# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

import sys
from pathlib import Path

UTIL_DIR = Path(__file__).resolve().parents[1] / "util"
sys.path.insert(0, str(UTIL_DIR))

import npe_analyze_noc_trace_dir


def test_trace_pool_recycles_worker_after_each_trace(monkeypatch):
    pool_arguments = {}

    class FakePool:
        def __init__(self, **kwargs):
            pool_arguments.update(kwargs)

    monkeypatch.setattr(npe_analyze_noc_trace_dir, "Pool", FakePool)

    npe_analyze_noc_trace_dir.create_trace_pool(num_workers=4)

    assert pool_arguments == {"processes": 4, "maxtasksperchild": 1}
