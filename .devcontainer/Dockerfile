FROM mcr.microsoft.com/devcontainers/python:3.11

# Install PlatformIO Core and Protobuf Generator Tools
RUN curl -fsSL -o get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py
RUN python3 get-platformio.py
ENV PATH="/root/.platformio/penv/bin:${PATH}"
RUN pip install protobuf grpcio-tools
