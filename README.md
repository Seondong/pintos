# Pintos

Operating Systems and Lab (CS330) by Insik Shin, [KAIST](http://www.kaist.ac.kr), Spring 2015.

## Environments

- Use docker on [yhpark/pintos-kaist](https://registry.hub.docker.com/u/yhpark/pintos-kaist/) (Recommended)
    - `docker pull yhpark/pintos-kaist`
    - `docker run -i -t --rm -v <PATH_TO_PINTOS>:/pintos yhpark/pintos-kaist bash`
    - If you want to run Docker as a non-root user `<USER>`, put `entry.sh` to `pintos`:

        ``` sh
        #!/bin/bash
        yum install -y sudo
        useradd -u <UID> <USER>
        echo '<USER> ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers
        su <USER> --session-command bash
        ```
      and then run `docker run -i -t --rm -v <PATH_TO_PINTOS>:/pintos yhpark/pintos-kaist /pintos/entry.sh`.
- Linux
    - Install X development libraries
    - Install development tools
        - Including gcc, make, perl, gdb (option) and so on...
- Bochs 2.6.6
    - Must use patches in pintos
    - Use script (pintos/src/misc/bochs-2.2.6-build.sh)

        ``` sh
        $ ./bochs-2.2.6-build.sh
        usage: env SRCDIR=<srcdir> PINTOSDIR=<srcdir> DSTDIR=<dstdir> sh ./bochs-2.2.6-build.sh
        where <srcdir> contains bochs-2.2.6.tar.gz
        and <pintosdir> is the root of the pintos source tree
        and <dstdir> is the installation prefix (e.g. /usr/local)
        $ env SRCDIR=/home/hahman5/bochs-2.2.6/ PINTOSDIR=/home/hahaman5/pintos DSTDIR=/usr/local/ sh /home/hahaman5/pintos/src/bochs-2.2.6-build.sh
        ```
    - Maybe need the latest version of Binutils & Make

## Install Pintos

- Get the source from <http://cps.kaist.ac.kr/courses/2015_spring_cs330/sources/pintos.tar.gz>.
  If the URL is not available, use local [pintos.tar.gz](pintos.tar.gz).
- Extract the source for Pintos into `pintos/src`:

    ``` sh
    tar xzf pintos.tar.gz
    ```
- Test your installation

    ``` sh
    cd pintos/src/threads
    make
    ../utils/pintos run alarm-multiple
    ```

## Running Pintos

- Add `pintos/src/utils` folder to `$PATH`
- Execute `pintos [option] -- [argument]`
    - Option
        - Configuring the simulator or the virtual hardware
    - Argument
        - Each argument is passed to Pintos kernel verbatim
        - `pintos run alarm-multiple` instructs the kernel to run alarm-multiple
        - `pintos -v -- -q run alarm multiple` clears bochs after execution
    - Pintos script
        - Parse command line
        - Find disks
        - Prepare arguments
        - Run VM (bochs)

## Testing Pintos

### Testing

- `make check` (in build directory)
- `make grade`

### Tests directory

`pintos/src/threads/build/tests`

### Graded result

`pintos/src/threads/build/grade`
