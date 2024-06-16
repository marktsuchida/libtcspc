# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import nox

nox.options.sessions = ["test"]


@nox.session(python=["3.10", "3.11", "3.12"])
@nox.parametrize("numpy2", [False, True])
def test(session, numpy2):
    session.install(".[testing]")
    session.install("numpy>=2" if numpy2 else "numpy<2")
    session.run("pytest")
