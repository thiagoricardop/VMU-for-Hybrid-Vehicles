FROM ubuntu:latest

# Install initial dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    wget \
    libgmp-dev \
    libmpfr-dev \
    libmpc-dev \
    libisl-dev \
    check \
    git \
    perl \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Install GCC 14.2.0
RUN apt-get update && apt-get install -y --no-install-recommends software-properties-common
RUN add-apt-repository ppa:ubuntu-toolchain-r/test -y
RUN apt-get update
RUN apt-get install -y --no-install-recommends gcc-14 g++-14 \
    libcunit1 libcunit1-doc libcunit1-dev
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100
RUN update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100
RUN update-alternatives --install /usr/bin/gcov gcov /usr/bin/gcov-14 100
RUN gcc --version
RUN g++ --version
RUN gcov --version

# Install LCOV 2.3-1
RUN apt-get install -y --no-install-recommends cpanminus
RUN curl -LO https://github.com/linux-test-project/lcov/releases/download/v2.3/lcov-2.3.tar.gz
RUN tar -xzf lcov-2.3.tar.gz
WORKDIR /lcov-2.3
RUN make install
RUN cpanm Capture::Tiny
RUN cpanm DateTime
RUN cpanm Date::Parse
RUN lcov --version

# Copy project files
COPY . /app

# Set working directory
WORKDIR /app

# Default command to build and run coverage (adjust as needed)
CMD ["make", "coverage"]