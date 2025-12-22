# SPDX-License-Identifier: GPL-2.0+
#
# Entry-type module for Embassy Preempt RTOS binary blob
#

from binman.etype.blob_named_by_arg import Entry_blob_named_by_arg

class Entry_embassy_preempt(Entry_blob_named_by_arg):
    """RISC-V Embassy Preempt RTOS blob

    Properties / Entry arguments:
        - embassy-preempt-path: Filename of file to read into entry. This is typically
            called embassy_preempt.bin or prio_test.bin

    This entry holds the Embassy Preempt RTOS that runs on HART0 alongside U-Boot
    on other harts. The RTOS is loaded by OpenSBI and runs independently of U-Boot.
    """
    def __init__(self, section, etype, node):
        super().__init__(section, etype, node, 'embassy-preempt')
        self.external = True