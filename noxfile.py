# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import nox

nox.options.sessions = ["test"]


@nox.session(python=["3.10", "3.11", "3.12", "3.13"])
def test(session):
    session.install(".[testing]")
    session.run("pytest")
