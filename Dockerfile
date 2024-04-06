FROM archlinux:base-devel

RUN pacman-key --init
RUN pacman-key --populate archlinux
RUN pacman-key --recv BC26F752D25B92CE272E0F44F7FD5492264BB9D0 --keyserver keyserver.ubuntu.com
RUN pacman-key --recv C8A2759C315CFBC3429CC2E422B803BA8AA3D7CE --keyserver keyserver.ubuntu.com
RUN pacman-key --lsign C8A2759C315CFBC3429CC2E422B803BA8AA3D7CE
RUN pacman-key --lsign BC26F752D25B92CE272E0F44F7FD5492264BB9D0

RUN echo "[dkp-libs]" >> /etc/pacman.conf 
RUN echo "Server = https://pkg.devkitpro.org/packages" >> /etc/pacman.conf 
RUN echo "[dkp-linux]" >> /etc/pacman.conf 
RUN echo "Server = https://pkg.devkitpro.org/packages/linux/x86_64/" >> /etc/pacman.conf 
RUN echo "[extremscorner-devkitpro]" >> /etc/pacman.conf 
RUN echo "Server = https://packages.extremscorner.org/devkitpro/linux/x86_64/" >> /etc/pacman.conf 

RUN pacman --noconfirm -Syu

RUN pacman --noconfirm -S --needed gcc git make flex bison gperf python cmake ninja ccache dfu-util libusb
RUN pacman --noconfirm -S --needed go python-pyelftools

RUN curl https://pkg.devkitpro.org/devkitpro-keyring.pkg.tar.xz >> devkitpro-keyring.pkg.tar.xz
RUN pacman --noconfirm -U devkitpro-keyring.pkg.tar.xz

RUN pacman --noconfirm -S gamecube-dev
RUN pacman --noconfirm -S libogc2
RUN pacman --noconfirm -S ppc-libpng

RUN echo "source /etc/profile.d/devkit-env.sh" >> /root/.bashrc
RUN echo "export PATH=/opt/devkitpro/devkitPPC/bin/:$PATH" >> /root/.bashrc

WORKDIR /build

# Keep the container running indefinitely
CMD ["tail", "-f", "/dev/null"]
