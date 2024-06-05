# Copyright © 2024 Matt Robinson
#
# SPDX-License-Identifier: GPL-2.0-or-later

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) modules

install:
	make -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) modules_install
	depmod -A

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) clean
