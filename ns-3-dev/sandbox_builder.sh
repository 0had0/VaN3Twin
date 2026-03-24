#!/bin/bash

NS3_VERSION=3.35

# Set NPROC based on OS
if [[ "$OSTYPE" == "darwin"* ]]; then
    NPROC=$(sysctl -n hw.ncpu)
else
    NPROC=$(nproc)
fi

install_port() {
    for pkg in "$@"; do
        if port installed "$pkg" 2>/dev/null | grep -q "active"; then
            echo "Package $pkg is already installed and active."
        else
            echo "Installing $pkg..."
            sudo port install "$pkg"
        fi
    done
}

install_pip() {
    for pkg in "$@"; do
        if uv pip show "${pkg%%==*}" >/dev/null 2>&1; then
            echo "Python package $pkg is already installed in uv environment."
        else
            echo "Installing python package $pkg via uv..."
            uv pip install "$pkg"
        fi
    done
}

setup_uv() {
    if ! command -v uv &> /dev/null; then
        echo "uv not found. Installing uv..."
        if [[ "$OSTYPE" == "darwin"* ]]; then
            sudo port install uv
        else
            curl -LsSf https://astral.sh/uv/install.sh | sh
            source $HOME/.cargo/env
        fi
    fi

    if [ ! -f "pyproject.toml" ]; then
        echo "Initializing uv project..."
        uv init --no-workspace --python 3.11
    fi
    
    # Ensure venv exists and is up to date
    uv venv --python 3.11
    export VIRTUAL_ENV=.venv
    export PATH="$PWD/.venv/bin:$PATH"
}

if [ $# -ne 0 -a $# -ne 1 ]; then
    echo "Too many arguments. You shall specify:"
    echo "	(Optional) 'install-dependencies' to attempt the installation of the ns-${NS3_VERSION} main dependencies"

    exit 1
fi

if [ "$EUID" -eq 0 ]; then
	echo "Please do NOT run this script as root."
	exit 1
fi

if [ -d "./ns-3-allinone" ]; then
    echo "Error: cannot proceed if there is a directory named ns-3-allinone is the current path."
    echo "Please rename it or move/launch the script from another directory."

    exit 1
fi

read -p "Press ENTER to continue with the installation..."

if [ ! -z $1 ]; then
	if [ $1 == "install-dependencies" ]; then
		echo "Installing some of the main dependencies for ns-${NS3_VERSION}..."
		setup_uv
		
		if [[ "$OSTYPE" == "darwin"* ]]; then
			echo "Detected macOS - using MacPorts"
			sudo port selfupdate
			
			MACPORTS_PKGS=(
				gcc13 python311 cmake ninja git ccache gobject-introspection 
				goocanvas2 gtk3 pkgconfig sqlite3 py311-setuptools qt5-qtbase 
				mercurial unzip gdb valgrind clang-17 doxygen graphviz 
				imagemagick py311-sphinx gsl libxml2 boost grpc protobuf3-cpp 
				libssh py311-gobject3 py311-cairo py311-graphviz tbb uv
			)
#			install_port "${MACPORTS_PKGS[@]}"
			
			install_pip grpcio grpcio-tools "conan==1.54.0"
		else
			echo "Detected Linux - using apt"
			sudo apt update
			if [ $? -ne 0 ]; then
				echo "Error: cannot run 'apt update'."
				exit 1
			fi

			sudo apt install -y g++ python3 cmake ninja-build git ccache gir1.2-goocanvas-2.0 python3-gi python3-gi-cairo python3-pygraphviz gir1.2-gtk-3.0 python3-dev pkg-config sqlite3 cmake python3-setuptools qtbase5-dev qtchooser qt5-qmake qtbase5-dev-tools mercurial unzip gdb valgrind clang clang-format doxygen graphviz imagemagick python3-sphinx gsl-bin libgsl-dev libgslcblas0 tcpdump libsqlite3-dev libxml2 libxml2-dev libc6-dev libc6-dev-i386 libclang-dev llvm-dev automake python3-pip libgtk-3-dev libxml2 libxml2-dev libboost-all-dev libgrpc++-dev libprotobuf-dev protobuf-compiler libdpdk-dev libssh-dev
			sudo apt install build-essential autoconf libtool pkg-config cmake
			install_pip grpcio grpcio-tools "conan==1.54.0"
		fi

		echo "Installing gRPC from source..."
		git clone -b v1.60.0 https://github.com/grpc/grpc
		cd grpc
 		git submodule update --init

		mkdir -p cmake/build
		cd cmake/build
		cmake ../..
		make -j$NPROC
		sudo make install
		cd ../../..

		echo "Installing OpenCV..."
		git clone https://github.com/opencv/opencv.git
	    git clone https://github.com/opencv/opencv_contrib.git
	    cd opencv
	    mkdir build
	    cd build
	    cmake -D CMAKE_BUILD_TYPE=Release \
	          -D CMAKE_INSTALL_PREFIX=/usr/local \
	          -D OPENCV_EXTRA_MODULES_PATH=../../opencv_contrib/modules \
	          -D WITH_TBB=ON \
	          -D WITH_V4L=OFF \
	          -D WITH_QT=OFF \
	          -D WITH_OPENGL=ON ..
	    make -j$NPROC
	    sudo make install
		if [[ "$OSTYPE" != "darwin"* ]]; then
	    	ldconfig
		fi
	    cd ../..

		# Detecting the current Ubuntu version to install the correct version of libgsl
		# This is done only on Ubuntu (i.e. only if the command "lsb_release" returns "Ubuntu" as distro)
		# If Ubuntu >= 20.10 is detected, libgsl25 is installed, otherwise libgsl23 is installed
		# If the distro is not detected to be Ubuntu, for the time being, the script attempts to install libgsl23
		# Please report any bug when using "install-dependencies": we will try to fix it as soon as possible!
		if [[ "$OSTYPE" != "darwin"* ]]; then
			lsb_release -d 2>/dev/null | grep Ubuntu > /dev/null
			
			if [ $? -eq 0 ]; then
				version=$(lsb_release -d | cut -d " " -f2 | cut -d "." -f1)
				subversion=$(lsb_release -d | cut -d " " -f2 | cut -d "." -f2)
				
				if [ $version -gt 20 -a $version -lt 22 -o \( $version -ge 20 -a $subversion -ge 10 \) ]; then
					echo "Detected Ubuntu >= 20.10 and Ubuntu < 22.04 - installing libgsl25"
					sudo apt install libgsl25
				elif [ $version -gt 22 -o \( $version -ge 22 -a $subversion -ge 04 \) ]; then
					echo "Detected Ubuntu >= 22.04 - installing libgsl27"
					sudo apt install libgsl27
				else
					echo "Detected Ubuntu < 20.10 - installing libgsl23"
					sudo apt install libgsl23
				fi
			else
				echo "Detected a distribution different than Ubuntu - installing libgsl23"
				sudo apt install libgsl23
			fi
		fi

		if [ $? -ne 0 ]; then
			echo "Error: cannot install the dependencies."
			exit 1
		fi
	else
		echo "Error: the second optional argument can be equal to \"install-dependencies\" or left empty only."
		echo "\"$1\" is not a valid optional argument."
		exit 1
	fi
fi

echo "Downloading ns-${NS3_VERSION} from the official repository..."
setup_uv
sleep 1
set -v
git clone https://gitlab.com/cttc-lena/ns-3-dev.git
cd ns-3-dev/src/
git clone https://gitlab.com/cttc-lena/nr.git
cd nr
git checkout nr-v2x-dev
find . -type d -name .git -exec rm -rfv {} \;
find . -type f -name .gitignore -exec rm -rfv {} \;
find . -type f -name .gitlab-ci-clang.yml -exec rm -rfv {} \;
find . -type f -name .gitlab-ci-gcc.yml -exec rm -rfv {} \;
find . -type f -name .gitlab-ci.yml -exec rm -rfv {} \;
find . -type f -name .gitmodules -exec rm -rfv {} \;
cd ../..
git checkout ns-3-dev-v2x-v0.2
set +v

echo "Removing git from vanilla ns-${NS3_VERSION}..."
sleep 1
set -v
find . -type d -name .git -exec rm -rfv {} \;
find . -type f -name .gitignore -exec rm -rfv {} \;
find . -type f -name .gitattributes -exec rm -rfv {} \;
set +v

echo "Installing the sandbox..."
sleep 1
set -v
cd ..
shopt -s extglob
shopt -s dotglob
cp -af ./!(ns-3-dev) ns-3-dev/
set +v

echo "Patching CMakeLists.txt to solve compatibility issue with C source files"
sleep 1
set -v
cd ns-3-dev
if [[ "$OSTYPE" == "darwin"* ]]; then
    sed -i '' -E 's#^([[:blank:]]*)project\(NS3 CXX\)#\1project\(NS3 C CXX\)#' CMakeLists.txt
else
    sed -i -E 's#^([[:blank:]]*)project\(NS3 CXX\)#\1project\(NS3 C CXX\)#' CMakeLists.txt
fi

# Ensure CMake uses the uv Python environment
PYTHON_PATH=$(which python3)
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DNS3_PYTHON_BINDINGS=ON -DPython3_EXECUTABLE="$PYTHON_PATH" .

cd ..
set +v

echo "Moving the full installation to the current directory..."
sleep 1
set -v
rm -rfv AUTHORS .git .gitignore img LICENSE license_gplv2.txt README.md src VERSION enable_v2x_emulator.sh
set +v

echo "Extending available path loss models..."
sleep 1
set -v
cd ns-3-dev
cp src/automotive/propagation-extended/cni-urbanmicrocell-propagation-loss-model.cc src/propagation/model/
cp src/automotive/propagation-extended/cni-urbanmicrocell-propagation-loss-model.h src/propagation/model/
cp src/automotive/propagation-extended/CMakeLists.txt src/propagation/

echo "Copying propagation files for NVIDIA Sionna..."
sleep 1
cp src/sionna/files/propagation/CMakeLists.txt src/propagation/
cp src/sionna/files/propagation/propagation-delay-model.cc src/propagation/model/
cp src/sionna/files/propagation/propagation-delay-model.h src/propagation/model/
cp src/sionna/files/propagation/propagation-loss-model.cc src/propagation/model/
cp src/sionna/files/propagation/propagation-loss-model.h src/propagation/model/
cp src/sionna/files/propagation/three-gpp-propagation-loss-model.h src/propagation/model
cp src/sionna/files/propagation/three-gpp-propagation-loss-model.cc src/propagation/model
cp src/sionna/files/spectrum/CMakeLists.txt src/spectrum/
cp src/sionna/files/spectrum/three-gpp-spectrum-propagation-loss-model.cc src/spectrum/model

cp src/automotive/model/TxTracker/channel_files/modified/yans-wifi-phy.h src/wifi/model

echo "Extending Signal Info features..."
sleep 1
cp src/automotive/model/SignalInfo/rssi-tag.cc src/wifi/model/
cp src/automotive/model/SignalInfo/rssi-tag.cc src/cv2x/model/
cp src/automotive/model/SignalInfo/rssi-tag.cc src/nr/model/
cp src/automotive/model/SignalInfo/rssi-tag.cc src/lte/model/
cp src/automotive/model/SignalInfo/rssi-tag.h src/wifi/model/
cp src/automotive/model/SignalInfo/rssi-tag.h src/cv2x/model/
cp src/automotive/model/SignalInfo/rssi-tag.h src/nr/model/
cp src/automotive/model/SignalInfo/rssi-tag.h src/lte/model/
cp src/automotive/model/SignalInfo/timestamp-tag.cc src/wifi/model/
cp src/automotive/model/SignalInfo/timestamp-tag.cc src/cv2x/model/
cp src/automotive/model/SignalInfo/timestamp-tag.cc src/nr/model/
cp src/automotive/model/SignalInfo/timestamp-tag.cc src/lte/model/
cp src/automotive/model/SignalInfo/timestamp-tag.h src/wifi/model/
cp src/automotive/model/SignalInfo/timestamp-tag.h src/cv2x/model/
cp src/automotive/model/SignalInfo/timestamp-tag.h src/nr/model/
cp src/automotive/model/SignalInfo/timestamp-tag.h src/lte/model/
cp src/automotive/model/SignalInfo/rsrp-tag.cc src/wifi/model/
cp src/automotive/model/SignalInfo/rsrp-tag.cc src/cv2x/model/
cp src/automotive/model/SignalInfo/rsrp-tag.cc src/nr/model/
cp src/automotive/model/SignalInfo/rsrp-tag.cc src/lte/model/
cp src/automotive/model/SignalInfo/rsrp-tag.h src/wifi/model/
cp src/automotive/model/SignalInfo/rsrp-tag.h src/cv2x/model/
cp src/automotive/model/SignalInfo/rsrp-tag.h src/nr/model/
cp src/automotive/model/SignalInfo/rsrp-tag.h src/lte/model/
cp src/automotive/model/SignalInfo/sinr-tag.cc src/wifi/model/
cp src/automotive/model/SignalInfo/sinr-tag.cc src/cv2x/model/
cp src/automotive/model/SignalInfo/sinr-tag.cc src/nr/model/
cp src/automotive/model/SignalInfo/sinr-tag.cc src/lte/model/
cp src/automotive/model/SignalInfo/sinr-tag.h src/wifi/model/
cp src/automotive/model/SignalInfo/sinr-tag.h src/cv2x/model/
cp src/automotive/model/SignalInfo/sinr-tag.h src/nr/model/
cp src/automotive/model/SignalInfo/sinr-tag.h src/lte/model/
cp src/automotive/model/SignalInfo/size-tag.cc src/wifi/model/
cp src/automotive/model/SignalInfo/size-tag.cc src/cv2x/model/
cp src/automotive/model/SignalInfo/size-tag.cc src/nr/model/
cp src/automotive/model/SignalInfo/size-tag.cc src/lte/model/
cp src/automotive/model/SignalInfo/size-tag.h src/wifi/model/
cp src/automotive/model/SignalInfo/size-tag.h src/cv2x/model/
cp src/automotive/model/SignalInfo/size-tag.h src/nr/model/
cp src/automotive/model/SignalInfo/size-tag.h src/lte/model/

cp src/automotive/model/SignalInfo/WiFi/wifi-mac-queue-item.h src/wifi/model/
cp src/automotive/model/SignalInfo/WiFi/ocb-wifi-mac.cc src/wave/model/
cp src/automotive/model/SignalInfo/WiFi/frame-exchange-manager.cc src/wifi/model/
cp src/automotive/model/SignalInfo/WiFi/qos-frame-exchange-manager.cc src/wifi/model/
cp src/automotive/model/SignalInfo/WiFi/CMakeLists.txt src/wifi/

cp src/automotive/model/SignalInfo/CV2X/cv2x_lte-spectrum-phy.cc src/cv2x/model/
cp src/automotive/model/SignalInfo/CV2X/cv2x_lte-spectrum-phy.h src/cv2x/model/
cp src/automotive/model/SignalInfo/CV2X/cv2x_lte-ue-mac.h src/cv2x/model/
cp src/automotive/model/SignalInfo/CV2X/cv2x_lte-ue-mac.cc src/cv2x/model/
cp src/automotive/model/SignalInfo/CV2X/CMakeLists.txt src/cv2x/

cp src/automotive/model/SignalInfo/NR/nr-spectrum-phy.cc src/nr/model/
cp src/automotive/model/SignalInfo/NR/nr-spectrum-phy.h src/nr/model/
cp src/automotive/model/SignalInfo/NR/nr-ue-phy.cc src/nr/model/
cp src/automotive/model/SignalInfo/NR/CMakeLists.txt src/nr/

cp src/automotive/model/SignalInfo/LTE/lte-spectrum-phy.cc src/lte/model/
cp src/automotive/model/SignalInfo/LTE/lte-ue-phy.cc src/lte/model/
cp src/automotive/model/SignalInfo/LTE/lte-ue-phy.h src/lte/model/
cp src/automotive/model/SignalInfo/LTE/CMakeLists.txt src/lte/

cd ..
set +v

echo "Installation completed. You will find a copy of this script in ./ns-3-dev."

rm $0
