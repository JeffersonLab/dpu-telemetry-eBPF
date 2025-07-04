## Using `pktgen` to generate huge amount of UDP packets at maximum speed

I was only able to install it on the JLab "nvidarm" node.

1. `pktgen` is built as a kernel module. Check if it’s already loaded:

    ```bash
    lsmod | grep pktgen
    ```
    If not, load it:

    ```bash
    sudo modprobe pktgen
    ```

    Check if the module is available:
    ```bash
    modinfo pktgen
    ```
2. Run the `pktgen` experiments within the [scripts](../scripts/) folder. Remember after each run, clear the `pktgen` setting with [clear-all-kpktgend.sh](../scripts/clear-all-kpktgend.sh). The scripts already assigned core number explicitly. 