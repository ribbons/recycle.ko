# Copyright © 2024-2026 Matt Robinson
#
# SPDX-License-Identifier: GPL-2.0-or-later

MODLIB := /lib/modules/$(shell uname -r)

all:
	make -C $(MODLIB)/build M=$(CURDIR) modules

install:
	make -C $(MODLIB)/build M=$(CURDIR) modules_install
	depmod -A

clean:
	make -C $(MODLIB)/build M=$(CURDIR) clean
