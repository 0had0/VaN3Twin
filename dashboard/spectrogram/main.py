"""
SPS Sensing Spectrogram Backend
Serves Freq x Time RSRP heatmap data from the SQLite metrics DB.
"""

import os
import sqlite3
from typing import Optional
from fastapi import FastAPI, Query
from fastapi.responses import HTMLResponse
from fastapi.templating import Jinja2Templates
from fastapi.staticfiles import StaticFiles
from starlette.requests import Request

app = FastAPI(title="SPS Sensing Spectrogram")
templates = Jinja2Templates(directory="templates")

DB_PATH = os.environ.get("DB_PATH", "/data/sps/sps_metrics.db")


def get_db():
    """Get a read-only SQLite connection."""
    conn = sqlite3.connect(f"file:{DB_PATH}?mode=ro", uri=True)
    conn.row_factory = sqlite3.Row
    return conn


@app.get("/", response_class=HTMLResponse)
async def index(request: Request, rnti: Optional[int] = None):
    """Serve the main spectrogram page."""
    return templates.TemplateResponse("index.html", {
        "request": request,
        "initial_rnti": rnti,
    })


@app.get("/api/rnti-list")
def rnti_list():
    """Return list of all RNTIs that have sensing data."""
    db = get_db()
    try:
        rows = db.execute(
            "SELECT DISTINCT rnti FROM sensing ORDER BY rnti"
        ).fetchall()
        return {"rntis": [r["rnti"] for r in rows]}
    finally:
        db.close()


@app.get("/api/config")
def sim_config():
    """Infer simulation config from the DB."""
    db = get_db()
    try:
        # Infer number of subchannels from max(sbch_start + sbch_length)
        row = db.execute(
            "SELECT MAX(sensed_sbch_start + sensed_sbch_length) as max_sbch "
            "FROM sensing LIMIT 1"
        ).fetchone()
        num_subchannels = row["max_sbch"] if row and row["max_sbch"] else 10

        # Infer time range
        row = db.execute(
            "SELECT MIN(sensed_sfn_norm) as t_min, MAX(sensed_sfn_norm) as t_max "
            "FROM sensing"
        ).fetchone()
        sfn_min = row["t_min"] if row else 0
        sfn_max = row["t_max"] if row else 0

        # Count vehicles
        row = db.execute("SELECT COUNT(DISTINCT rnti) as n FROM sensing").fetchone()
        num_vehicles = row["n"] if row else 0

        return {
            "num_subchannels": num_subchannels,
            "sfn_min": sfn_min,
            "sfn_max": sfn_max,
            "num_vehicles": num_vehicles,
        }
    finally:
        db.close()


@app.get("/api/heatmap-data")
def heatmap_data(
    rnti: int = Query(..., description="Vehicle RNTI"),
    sfn_from: Optional[int] = Query(None, description="Start SFN (inclusive)"),
    sfn_to: Optional[int] = Query(None, description="End SFN (inclusive)"),
):
    """
    Return sensing heatmap data for a vehicle.

    Returns a sparse list of (sfn, subchannel, rsrp) triples,
    with subchannel ranges expanded into individual entries.
    Deduplicated: only the latest dump per (sfn, subchannel).
    """
    db = get_db()
    try:
        # Build query with optional time range filter
        where = "WHERE rnti = ?"
        params = [rnti]
        if sfn_from is not None:
            where += " AND sensed_sfn_norm >= ?"
            params.append(sfn_from)
        if sfn_to is not None:
            where += " AND sensed_sfn_norm <= ?"
            params.append(sfn_to)

        # Use recursive CTE to expand subchannel ranges,
        # then deduplicate keeping latest dump per (sfn, subchannel)
        query = f"""
        WITH RECURSIVE expanded AS (
            SELECT dump_sfn_norm, sensed_sfn_norm,
                   sensed_sbch_start AS subchannel,
                   sensed_sbch_start + sensed_sbch_length - 1 AS last_sbch,
                   sensed_rsrp
            FROM sensing
            {where}

            UNION ALL

            SELECT dump_sfn_norm, sensed_sfn_norm,
                   subchannel + 1, last_sbch,
                   sensed_rsrp
            FROM expanded
            WHERE subchannel < last_sbch
        ),
        deduped AS (
            SELECT sensed_sfn_norm, subchannel, sensed_rsrp,
                   ROW_NUMBER() OVER (
                       PARTITION BY sensed_sfn_norm, subchannel
                       ORDER BY dump_sfn_norm DESC
                   ) AS rn
            FROM expanded
        )
        SELECT sensed_sfn_norm AS sfn, subchannel, sensed_rsrp AS rsrp
        FROM deduped
        WHERE rn = 1
        ORDER BY sfn, subchannel
        """
        rows = db.execute(query, params).fetchall()

        return {
            "data": [
                {"sfn": r["sfn"], "subchannel": r["subchannel"], "rsrp": r["rsrp"]}
                for r in rows
            ],
            "count": len(rows),
        }
    finally:
        db.close()


@app.get("/api/selection-events")
def selection_events(
    rnti: int = Query(..., description="Vehicle RNTI"),
):
    """Return selection events for a vehicle (threshold info, +3dB events)."""
    db = get_db()
    try:
        rows = db.execute(
            """SELECT timestamp_ms, sfn_normalized, resource_reused,
                      csr_a_total, csr_a_after_exclusion,
                      threshold_iterations, final_threshold_dBm,
                      sensing_window_size, selected_slot_norm
               FROM selection
               WHERE rnti = ?
               ORDER BY sfn_normalized""",
            [rnti],
        ).fetchall()
        return {
            "events": [dict(r) for r in rows],
            "count": len(rows),
        }
    finally:
        db.close()


@app.get("/api/threshold-relaxations")
def threshold_relaxations(
    rnti: int = Query(..., description="Vehicle RNTI"),
):
    """Return only events where threshold was relaxed (+3dB iterations)."""
    db = get_db()
    try:
        rows = db.execute(
            """SELECT sfn_normalized, threshold_iterations, final_threshold_dBm,
                      csr_a_total, csr_a_after_exclusion
               FROM selection
               WHERE rnti = ? AND threshold_iterations > 0
               ORDER BY sfn_normalized""",
            [rnti],
        ).fetchall()
        return {"events": [dict(r) for r in rows]}
    finally:
        db.close()
