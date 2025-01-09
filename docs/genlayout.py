#!python3
# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import sys
import xml.etree.ElementTree as ET

infile, outfile = sys.argv[1:]

tree = ET.parse(infile)
root = tree.getroot()

assert root.tag == "doxygenlayout"

# For each section, remove {detailed,brief}description and re-add
# detaileddescription at top. Skip section if it does not contain
# detaileddescription (navindex).

for section in root:
    if section.tag == "navindex":
        for ns_tab in section.findall("./tab[@type='namespaces']"):
            for ns_memb_tab in ns_tab.findall(
                "./tab[@type='namespacemembers']"
            ):
                ns_tab.remove(ns_memb_tab)
        for classes_tab in section.findall("./tab[@type='classes']"):
            section.remove(classes_tab)
    else:
        had_dd = False
        for dd in section.findall("detaileddescription"):
            had_dd = True
            section.remove(dd)
        if not had_dd:
            continue
        had_bd = False
        for bd in section.findall("briefdescription"):
            had_bd = True
            bd.set("vislble", "no")
        assert had_bd
        section.insert(
            0,
            ET.Element(
                "detaileddescription",
                {"title": "Description", "visible": "yes"},
            ),
        )

tree.write(outfile)
