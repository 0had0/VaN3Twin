/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 *   NR V2X Mode 2 SB-SPS motorway scenario for per-vehicle metrics export.
 *   Based on v2v-congestion-nrv2x.cc with sensing enabled by default.
 */

#include "ns3/automotive-module.h"
#include "ns3/traci-module.h"
#include "ns3/config-store.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/mobility-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/nr-module.h"
#include "ns3/lte-module.h"
#include "ns3/stats-module.h"
#include "ns3/config-store-module.h"
#include "ns3/log.h"
#include "ns3/antenna-module.h"
#include <iomanip>
#include "ns3/sumo_xml_parser.h"
#include "ns3/vehicle-visualizer-module.h"
#include "ns3/MetricSupervisor.h"

#include <unistd.h>
#include <sys/stat.h>
#include "ns3/core-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("v2v-motorway-nrv2x");

void
GetSlBitmapFromString (std::string slBitMapString, std::vector <std::bitset<1> > &slBitMapVector)
{
  static std::unordered_map<std::string, uint8_t> lookupTable =
  {
    { "0", 0 },
    { "1", 1 },
  };

  std::stringstream ss (slBitMapString);
  std::string token;
  std::vector<std::string> extracted;

  while (std::getline (ss, token, '|'))
    {
      extracted.push_back (token);
    }

  for (const auto & v : extracted)
    {
      if (lookupTable.find (v) == lookupTable.end ())
        {
          NS_FATAL_ERROR ("Bit type " << v << " not valid. Valid values are: 0 and 1");
        }
      slBitMapVector.push_back (lookupTable[v] & 0x01);
    }
}


int
main (int argc, char *argv[])
{
  std::string sumo_folder = "src/automotive/examples/sumo_files_motorway/";
  std::string mob_trace = "vehicles.rou.xml";
  std::string sumo_config = "src/automotive/examples/sumo_files_motorway/motorway.sumo.cfg";

  /*** 0.a App Options ***/
  bool verbose = true;
  bool realtime = false;
  bool sumo_gui = true;
  double sumo_updates = 0.01;
  std::string csv_name;
  std::string csv_name_cumulative;
  std::string sumo_netstate_file_name;
  bool vehicle_vis = false;

  int numberOfNodes;
  uint32_t nodeCounter = 0;

  double penetrationRate = 1.0;  // All vehicles equipped

  xmlDocPtr rou_xml_file;
  double m_baseline_prr = 150.0;
  bool m_metric_sup = true;

  // Simulation parameters
  double simTime = 60.0;
  Time slBearersActivationTime = Seconds (2.0);

  // NR parameters — 5.89 GHz band n47, sensing ENABLED for SB-SPS
  double centralFrequencyBandSl = 5.89e9;
  uint16_t bandwidthBandSl = 400;
  double txPower = 23; //dBm
  std::string tddPattern = "UL|UL|UL|UL|UL|UL|UL|UL|UL|UL|";
  std::string slBitMap = "1|1|1|1|1|1|1|1|1|1";
  uint16_t numerologyBwpSl = 2;
  uint16_t slSensingWindow = 100; // T0 in ms
  uint16_t slSelectionWindow = 5; // T2min
  uint16_t slSubchannelSize = 10;
  uint16_t slMaxNumPerReserve = 3;
  double slProbResourceKeep = 0.4;
  uint16_t slMaxTxTransNumPssch = 5;
  uint16_t reservationPeriod = 20; // in ms
  bool enableSensing = true;  // CRITICAL: sensing enabled for SB-SPS
  uint16_t t1 = 2;
  uint16_t t2 = 81;
  int slThresPsschRsrp = -128;
  bool enableChannelRandomness = false;
  uint16_t channelUpdatePeriod = 500; //ms
  uint8_t mcs = 14;

  // SPS logging
  bool enableSpsLog = true;
  std::string spsLogDir = "sps_logs/";

  CommandLine cmd;

  cmd.AddValue ("realtime", "Use the realtime scheduler or not", realtime);
  cmd.AddValue ("sumo-gui", "Use SUMO gui or not", sumo_gui);
  cmd.AddValue ("sumo-updates", "SUMO granularity", sumo_updates);
  cmd.AddValue ("sumo-folder", "Position of sumo config files", sumo_folder);
  cmd.AddValue ("mob-trace", "Name of the mobility trace file", mob_trace);
  cmd.AddValue ("sumo-config", "Location and name of SUMO configuration file", sumo_config);
  cmd.AddValue ("csv-log", "Name of the CSV log file", csv_name);
  cmd.AddValue ("vehicle-visualizer", "Activate the web-based vehicle visualizer", vehicle_vis);
  cmd.AddValue ("csv-log-cumulative", "Name of the CSV cumulative log file", csv_name_cumulative);
  cmd.AddValue ("netstate-dump-file", "Name of the SUMO netstate-dump file", sumo_netstate_file_name);
  cmd.AddValue ("baseline", "Baseline for PRR calculation", m_baseline_prr);
  cmd.AddValue ("met-sup", "Use the Metric supervisor or not", m_metric_sup);
  cmd.AddValue ("penetrationRate", "Rate of equipped vehicles", penetrationRate);

  cmd.AddValue ("simTime", "Simulation time in seconds", simTime);
  cmd.AddValue ("slBearerActivationTime", "Sidelink bearer activation time in seconds", slBearersActivationTime);
  cmd.AddValue ("centralFrequencyBandSl", "Central frequency for Sidelink band", centralFrequencyBandSl);
  cmd.AddValue ("bandwidthBandSl", "System bandwidth for Sidelink", bandwidthBandSl);
  cmd.AddValue ("txPower", "Total tx power in dBm", txPower);
  cmd.AddValue ("tddPattern", "The TDD pattern string", tddPattern);
  cmd.AddValue ("slBitMap", "The Sidelink bitmap string", slBitMap);
  cmd.AddValue ("numerologyBwpSl", "Numerology for Sidelink BWP", numerologyBwpSl);
  cmd.AddValue ("slSensingWindow", "Sidelink sensing window length in ms", slSensingWindow);
  cmd.AddValue ("slSelectionWindow", "Min Sidelink selection window length", slSelectionWindow);
  cmd.AddValue ("slSubchannelSize", "Sidelink subchannel size in RBs", slSubchannelSize);
  cmd.AddValue ("slMaxNumPerReserve", "Max number of reserved resources per SCI", slMaxNumPerReserve);
  cmd.AddValue ("slProbResourceKeep", "Probability of keeping current resource", slProbResourceKeep);
  cmd.AddValue ("slMaxTxTransNumPssch", "Max transmission number for PSSCH", slMaxTxTransNumPssch);
  cmd.AddValue ("ReservationPeriod", "Resource reservation period in ms", reservationPeriod);
  cmd.AddValue ("enableSensing", "Enable sensing-based resource selection", enableSensing);
  cmd.AddValue ("t1", "Start of selection window in physical slots", t1);
  cmd.AddValue ("t2", "End of selection window in physical slots", t2);
  cmd.AddValue ("slThresPsschRsrp", "Threshold in dBm for sensing-based selection", slThresPsschRsrp);
  cmd.AddValue ("enableChannelRandomness", "Enable shadowing and channel updates", enableChannelRandomness);
  cmd.AddValue ("channelUpdatePeriod", "Channel update period in ms", channelUpdatePeriod);
  cmd.AddValue ("mcs", "MCS for sidelink", mcs);
  cmd.AddValue ("enableSpsLog", "Enable per-vehicle SPS selection CSV logging", enableSpsLog);
  cmd.AddValue ("spsLogDir", "Directory for SPS log files", spsLogDir);

  cmd.Parse (argc, argv);

  if (verbose)
    {
      LogComponentEnable ("v2v-motorway-nrv2x", LOG_LEVEL_INFO);
      LogComponentEnable ("CABasicService", LOG_LEVEL_INFO);
      LogComponentEnable ("DENBasicService", LOG_LEVEL_INFO);
    }

  NS_ABORT_IF (centralFrequencyBandSl > 6e9);

  // Create output directory for SPS logs
  if (enableSpsLog)
    {
      mkdir (spsLogDir.c_str (), 0755);
    }

  Config::SetDefault ("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue (999999999));

  if (realtime)
    GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));

  /*** Read number of vehicles from rou file ***/
  NS_LOG_INFO ("Reading the .rou file...");
  std::string path = sumo_folder + mob_trace;

  xmlInitParser ();
  rou_xml_file = xmlParseFile (path.c_str ());
  if (rou_xml_file == NULL)
    {
      NS_FATAL_ERROR ("Error: unable to parse the specified XML file: " << path);
    }
  numberOfNodes = XML_rou_count_vehicles (rou_xml_file);
  xmlFreeDoc (rou_xml_file);
  xmlCleanupParser ();

  if (numberOfNodes == -1)
    {
      NS_FATAL_ERROR ("Fatal error: cannot gather the number of vehicles from: " << path);
    }
  NS_LOG_INFO ("The .rou file has been read: " << numberOfNodes << " vehicles will be present.");

  NodeContainer allSlUesContainer;
  allSlUesContainer.Create (numberOfNodes);

  MobilityHelper mobility;
  mobility.Install (allSlUesContainer);

  Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper> ();
  Ptr<NrHelper> nrHelper = CreateObject<NrHelper> ();
  nrHelper->SetEpcHelper (epcHelper);

  BandwidthPartInfoPtrVector allBwps;
  CcBwpCreator ccBwpCreator;
  const uint8_t numCcPerBand = 1;

  CcBwpCreator::SimpleOperationBandConf bandConfSl (centralFrequencyBandSl, bandwidthBandSl, numCcPerBand, BandwidthPartInfo::V2V_Highway);
  OperationBandInfo bandSl = ccBwpCreator.CreateOperationBandContiguousCc (bandConfSl);

  if (enableChannelRandomness)
    {
      Config::SetDefault ("ns3::ThreeGppChannelModel::UpdatePeriod", TimeValue (MilliSeconds (channelUpdatePeriod)));
      nrHelper->SetChannelConditionModelAttribute ("UpdatePeriod", TimeValue (MilliSeconds (channelUpdatePeriod)));
      nrHelper->SetPathlossAttribute ("ShadowingEnabled", BooleanValue (true));
    }
  else
    {
      Config::SetDefault ("ns3::ThreeGppChannelModel::UpdatePeriod", TimeValue (MilliSeconds (0)));
      nrHelper->SetChannelConditionModelAttribute ("UpdatePeriod", TimeValue (MilliSeconds (0)));
      nrHelper->SetPathlossAttribute ("ShadowingEnabled", BooleanValue (false));
    }

  nrHelper->InitializeOperationBand (&bandSl);
  allBwps = CcBwpCreator::GetAllBwps ({bandSl});

  nrHelper->SetUeAntennaAttribute ("NumRows", UintegerValue (1));
  nrHelper->SetUeAntennaAttribute ("NumColumns", UintegerValue (2));
  nrHelper->SetUeAntennaAttribute ("AntennaElement", PointerValue (CreateObject<IsotropicAntennaModel> ()));

  nrHelper->SetUePhyAttribute ("TxPower", DoubleValue (txPower));

  nrHelper->SetUeMacAttribute ("EnableSensing", BooleanValue (enableSensing));
  nrHelper->SetUeMacAttribute ("T1", UintegerValue (static_cast<uint8_t> (t1)));
  nrHelper->SetUeMacAttribute ("T2", UintegerValue (t2));
  nrHelper->SetUeMacAttribute ("ActivePoolId", UintegerValue (0));
  nrHelper->SetUeMacAttribute ("ReservationPeriod", TimeValue (MilliSeconds (reservationPeriod)));
  nrHelper->SetUeMacAttribute ("NumSidelinkProcess", UintegerValue (4));
  nrHelper->SetUeMacAttribute ("EnableBlindReTx", BooleanValue (true));
  nrHelper->SetUeMacAttribute ("SlThresPsschRsrp", IntegerValue (slThresPsschRsrp));

  // Enable SPS selection logging
  if (enableSpsLog)
    {
      nrHelper->SetUeMacAttribute ("EnableSpsSelectionLog", BooleanValue (true));
      nrHelper->SetUeMacAttribute ("SpsSelectionLogDir", StringValue (spsLogDir));
    }

  uint8_t bwpIdForGbrMcptt = 0;
  nrHelper->SetBwpManagerTypeId (TypeId::LookupByName ("ns3::NrSlBwpManagerUe"));
  nrHelper->SetUeBwpManagerAlgorithmAttribute ("GBR_MC_PUSH_TO_TALK", UintegerValue (bwpIdForGbrMcptt));

  std::set<uint8_t> bwpIdContainer;
  bwpIdContainer.insert (bwpIdForGbrMcptt);

  NetDeviceContainer allSlUesNetDeviceContainer = nrHelper->InstallUeDevice (allSlUesContainer, allBwps);

  for (auto it = allSlUesNetDeviceContainer.Begin (); it != allSlUesNetDeviceContainer.End (); ++it)
    {
      DynamicCast<NrUeNetDevice> (*it)->UpdateConfig ();
    }

  /*** Configure Sidelink ***/
  Ptr<NrSlHelper> nrSlHelper = CreateObject <NrSlHelper> ();
  nrSlHelper->SetEpcHelper (epcHelper);

  std::string errorModel = "ns3::NrLteMiErrorModel";
  nrSlHelper->SetSlErrorModel (errorModel);
  nrSlHelper->SetUeSlAmcAttribute ("AmcModel", EnumValue (NrAmc::ErrorModel));

  nrSlHelper->SetNrSlSchedulerTypeId (NrSlUeMacSchedulerSimple::GetTypeId ());
  nrSlHelper->SetUeSlSchedulerAttribute ("FixNrSlMcs", BooleanValue (true));
  nrSlHelper->SetUeSlSchedulerAttribute ("InitialNrSlMcs", UintegerValue (mcs));

  // Enable scheduler PRNG logging
  if (enableSpsLog)
    {
      nrSlHelper->SetUeSlSchedulerAttribute ("EnableSchedPrngLog", BooleanValue (true));
      nrSlHelper->SetUeSlSchedulerAttribute ("SchedPrngLogDir", StringValue (spsLogDir));
    }

  nrSlHelper->PrepareUeForSidelink (allSlUesNetDeviceContainer, bwpIdContainer);

  /*** Sidelink pre-configuration ***/
  LteRrcSap::SlResourcePoolNr slResourcePoolNr;
  Ptr<NrSlCommPreconfigResourcePoolFactory> ptrFactory = Create<NrSlCommPreconfigResourcePoolFactory> ();

  std::vector <std::bitset<1> > slBitMapVector;
  GetSlBitmapFromString (slBitMap, slBitMapVector);
  NS_ABORT_MSG_IF (slBitMapVector.empty (), "GetSlBitmapFromString failed to generate SL bitmap");
  ptrFactory->SetSlTimeResources (slBitMapVector);
  ptrFactory->SetSlSensingWindow (slSensingWindow);
  ptrFactory->SetSlSelectionWindow (slSelectionWindow);
  ptrFactory->SetSlFreqResourcePscch (10);
  ptrFactory->SetSlSubchannelSize (slSubchannelSize);
  ptrFactory->SetSlMaxNumPerReserve (slMaxNumPerReserve);
  LteRrcSap::SlResourcePoolNr pool = ptrFactory->CreatePool ();
  slResourcePoolNr = pool;

  LteRrcSap::SlResourcePoolConfigNr slresoPoolConfigNr;
  slresoPoolConfigNr.haveSlResourcePoolConfigNr = true;
  uint16_t poolId = 0;
  LteRrcSap::SlResourcePoolIdNr slResourcePoolIdNr;
  slResourcePoolIdNr.id = poolId;
  slresoPoolConfigNr.slResourcePoolId = slResourcePoolIdNr;
  slresoPoolConfigNr.slResourcePool = slResourcePoolNr;

  LteRrcSap::SlBwpPoolConfigCommonNr slBwpPoolConfigCommonNr;
  slBwpPoolConfigCommonNr.slTxPoolSelectedNormal [slResourcePoolIdNr.id] = slresoPoolConfigNr;

  LteRrcSap::Bwp bwp;
  bwp.numerology = numerologyBwpSl;
  bwp.symbolsPerSlots = 14;
  bwp.rbPerRbg = 1;
  bwp.bandwidth = bandwidthBandSl;

  LteRrcSap::SlBwpGeneric slBwpGeneric;
  slBwpGeneric.bwp = bwp;
  slBwpGeneric.slLengthSymbols = LteRrcSap::GetSlLengthSymbolsEnum (14);
  slBwpGeneric.slStartSymbol = LteRrcSap::GetSlStartSymbolEnum (0);

  LteRrcSap::SlBwpConfigCommonNr slBwpConfigCommonNr;
  slBwpConfigCommonNr.haveSlBwpGeneric = true;
  slBwpConfigCommonNr.slBwpGeneric = slBwpGeneric;
  slBwpConfigCommonNr.haveSlBwpPoolConfigCommonNr = true;
  slBwpConfigCommonNr.slBwpPoolConfigCommonNr = slBwpPoolConfigCommonNr;

  LteRrcSap::SlFreqConfigCommonNr slFreConfigCommonNr;
  for (const auto &it : bwpIdContainer)
    {
      slFreConfigCommonNr.slBwpList [it] = slBwpConfigCommonNr;
    }

  LteRrcSap::TddUlDlConfigCommon tddUlDlConfigCommon;
  tddUlDlConfigCommon.tddPattern = tddPattern;

  LteRrcSap::SlPreconfigGeneralNr slPreconfigGeneralNr;
  slPreconfigGeneralNr.slTddConfig = tddUlDlConfigCommon;

  LteRrcSap::SlUeSelectedConfig slUeSelectedPreConfig;
  NS_ABORT_MSG_UNLESS (slProbResourceKeep <= 1.0, "slProbResourceKeep value must be between 0 and 1");
  slUeSelectedPreConfig.slProbResourceKeep = slProbResourceKeep;

  LteRrcSap::SlPsschTxParameters psschParams;
  psschParams.slMaxTxTransNumPssch = static_cast<uint8_t> (slMaxTxTransNumPssch);
  LteRrcSap::SlPsschTxConfigList pscchTxConfigList;
  pscchTxConfigList.slPsschTxParameters [0] = psschParams;
  slUeSelectedPreConfig.slPsschTxConfigList = pscchTxConfigList;

  LteRrcSap::SidelinkPreconfigNr slPreConfigNr;
  slPreConfigNr.slPreconfigGeneral = slPreconfigGeneralNr;
  slPreConfigNr.slUeSelectedPreConfig = slUeSelectedPreConfig;
  slPreConfigNr.slPreconfigFreqInfoList [0] = slFreConfigCommonNr;

  nrSlHelper->InstallNrSlPreConfiguration (allSlUesNetDeviceContainer, slPreConfigNr);

  /*** Fix random streams ***/
  int64_t stream = 1;
  stream += nrHelper->AssignStreams (allSlUesNetDeviceContainer, stream);
  stream += nrSlHelper->AssignStreams (allSlUesNetDeviceContainer, stream);

  /*** All UEs transmit and receive ***/
  NodeContainer txSlUes;
  NodeContainer rxSlUes;
  NetDeviceContainer txSlUesNetDevice;
  NetDeviceContainer rxSlUesNetDevice;
  txSlUes.Add (allSlUesContainer);
  rxSlUes.Add (allSlUesContainer);
  txSlUesNetDevice.Add (allSlUesNetDeviceContainer);
  rxSlUesNetDevice.Add (allSlUesNetDeviceContainer);

  /*** IP stack ***/
  InternetStackHelper internet;
  internet.Install (allSlUesContainer);
  uint32_t dstL2Id = 255;
  Ipv4Address groupAddress4 ("225.0.0.0");

  Address remoteAddress;
  Address localAddress;
  uint16_t port = 8000;
  Ptr<LteSlTft> tft;

  Ipv4InterfaceContainer ueIpIface;
  ueIpIface = epcHelper->AssignUeIpv4Address (allSlUesNetDeviceContainer);

  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  for (uint32_t u = 0; u < allSlUesContainer.GetN (); ++u)
    {
      Ptr<Node> ueNode = allSlUesContainer.Get (u);
      Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting (ueNode->GetObject<Ipv4> ());
      ueStaticRouting->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (), 1);
    }
  remoteAddress = InetSocketAddress (groupAddress4, port);
  localAddress = InetSocketAddress (Ipv4Address::GetAny (), port);

  tft = Create<LteSlTft> (LteSlTft::Direction::TRANSMIT, LteSlTft::CommType::GroupCast, groupAddress4, dstL2Id);
  nrSlHelper->ActivateNrSlBearer (slBearersActivationTime, allSlUesNetDeviceContainer, tft);

  tft = Create<LteSlTft> (LteSlTft::Direction::RECEIVE, LteSlTft::CommType::GroupCast, groupAddress4, dstL2Id);
  nrSlHelper->ActivateNrSlBearer (slBearersActivationTime, allSlUesNetDeviceContainer, tft);

  /*** Setup TraCI and start SUMO ***/
  Ptr<TraciClient> sumoClient = CreateObject<TraciClient> ();
  sumoClient->SetAttribute ("SumoConfigPath", StringValue (sumo_config));
  sumoClient->SetAttribute ("SumoBinaryPath", StringValue (""));
  sumoClient->SetAttribute ("SynchInterval", TimeValue (Seconds (sumo_updates)));
  sumoClient->SetAttribute ("StartTime", TimeValue (Seconds (0.0)));
  sumoClient->SetAttribute ("SumoGUI", BooleanValue (sumo_gui));
  sumoClient->SetAttribute ("SumoPort", UintegerValue (3400));
  sumoClient->SetAttribute ("PenetrationRate", DoubleValue (penetrationRate));
  sumoClient->SetAttribute ("SumoLogFile", BooleanValue (false));
  sumoClient->SetAttribute ("SumoStepLog", BooleanValue (false));
  sumoClient->SetAttribute ("SumoSeed", IntegerValue (10));

  std::string sumo_additional_options = "--verbose true";
  if (sumo_netstate_file_name != "")
    {
      sumo_additional_options += " --netstate-dump " + sumo_netstate_file_name;
    }
  sumoClient->SetAttribute ("SumoAdditionalCmdOptions", StringValue (sumo_additional_options));
  sumoClient->SetAttribute ("SumoWaitForSocket", TimeValue (Seconds (1.0)));

  vehicleVisualizer vehicleVisObj;
  Ptr<vehicleVisualizer> vehicleVis = &vehicleVisObj;
  if (vehicle_vis)
    {
      vehicleVis->startServer ();
      vehicleVis->connectToServer ();
      sumoClient->SetAttribute ("VehicleVisualizer", PointerValue (vehicleVis));
    }

  Ptr<MetricSupervisor> metSup = NULL;
  MetricSupervisor metSupObj (m_baseline_prr);
  if (m_metric_sup)
    {
      metSup = &metSupObj;
      metSup->setTraCIClient (sumoClient);
      metSup->disablePRRVerboseOnStdout ();
      metSup->setChannelTechnology ("Nr");
      metSup->enableCBRVerboseOnStdout ();
      metSup->enableCBRWriteToFile ();
      metSup->setCBRWindowValue (100);
      metSup->setCBRAlphaValue (0.1);
      metSup->setSimulationTimeValue (simTime);
      metSup->startCheckCBR ();
    }

  /*** Setup application for dynamic nodes ***/
  emergencyVehicleAlertHelper EmergencyVehicleAlertHelper;
  EmergencyVehicleAlertHelper.SetAttribute ("Client", PointerValue (sumoClient));
  EmergencyVehicleAlertHelper.SetAttribute ("RealTime", BooleanValue (realtime));
  EmergencyVehicleAlertHelper.SetAttribute ("PrintSummary", BooleanValue (true));
  EmergencyVehicleAlertHelper.SetAttribute ("CSV", StringValue (csv_name));
  EmergencyVehicleAlertHelper.SetAttribute ("Model", StringValue ("nrv2x"));
  EmergencyVehicleAlertHelper.SetAttribute ("MetricSupervisor", PointerValue (metSup));
  EmergencyVehicleAlertHelper.SetAttribute ("SendCPM", BooleanValue (false));

  int i = 0;
  STARTUP_FCN setupNewWifiNode = [&] (std::string vehicleID, TraciClient::StationTypeTraCI_t stationType) -> Ptr<Node>
    {
      if (nodeCounter >= allSlUesContainer.GetN ())
        NS_FATAL_ERROR ("Node Pool empty!: " << nodeCounter << " nodes created.");

      Ptr<Node> includedNode = allSlUesContainer.Get (nodeCounter);
      ++nodeCounter;

      EmergencyVehicleAlertHelper.SetAttribute ("IpAddr", Ipv4AddressValue (groupAddress4));
      i++;

      ApplicationContainer AppSample = EmergencyVehicleAlertHelper.Install (includedNode);
      AppSample.Start (Seconds (0.0));
      AppSample.Stop (Seconds (simTime) - Simulator::Now () - Seconds (0.1));

      return includedNode;
    };

  SHUTDOWN_FCN shutdownWifiNode = [] (Ptr<Node> exNode, std::string vehicleID)
    {
      Ptr<emergencyVehicleAlert> appSample_ = exNode->GetApplication (0)->GetObject<emergencyVehicleAlert> ();
      if (appSample_)
        appSample_->StopApplicationNow ();

      Ptr<ConstantPositionMobilityModel> mob = exNode->GetObject<ConstantPositionMobilityModel> ();
      mob->SetPosition (Vector (-1000.0 + (rand () % 25), 320.0 + (rand () % 25), 250.0));
    };

  sumoClient->SumoSetup (setupNewWifiNode, shutdownWifiNode);

  /*** Start Simulation ***/
  Simulator::Stop (Seconds (simTime));
  Simulator::Run ();
  Simulator::Destroy ();

  if (m_metric_sup)
    {
      if (csv_name_cumulative != "")
        {
          std::ofstream csv_cum_ofstream;
          std::string full_csv_name = csv_name_cumulative + ".csv";

          if (access (full_csv_name.c_str (), F_OK) != -1)
            {
              csv_cum_ofstream.open (full_csv_name, std::ofstream::out | std::ofstream::app);
            }
          else
            {
              csv_cum_ofstream.open (full_csv_name);
              csv_cum_ofstream << "current_txpower_dBm,avg_PRR,avg_latency_ms" << std::endl;
            }
          csv_cum_ofstream << txPower << "," << metSup->getAveragePRR_overall () << "," << metSup->getAverageLatency_overall () << std::endl;
        }
      std::cout << "Average PRR: " << metSup->getAveragePRR_overall () << std::endl;
      std::cout << "Average latency (ms): " << metSup->getAverageLatency_overall () << std::endl;
    }

  return 0;
}
