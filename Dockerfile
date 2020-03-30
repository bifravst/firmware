FROM coderbyheart/fw-nrfconnect-nrf-docker:latest
RUN rm -rf /workdir/ncs
COPY . /workdir/ncs/nrf
RUN \
    # Zephyr requirements of nrf
    cd /workdir/ncs/nrf; west init -l && \
    cd /workdir/ncs; west update && \
    pip3 install -r zephyr/scripts/requirements.txt && \
    pip3 install -r nrf/scripts/requirements.txt && \
    pip3 install -r bootloader/mcuboot/scripts/requirements.txt && \
    echo "source /workdir/ncs/zephyr/zephyr-env.sh" >> ~/.bashrc && \
    mkdir /workdir/.cache && \
    rm -rf /workdir/ncs/nrf