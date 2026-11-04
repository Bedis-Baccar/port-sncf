import pytest
from pydantic import ValidationError

from tusp.solver import SolverParams


def test_defaults_map_to_branch_and_price_with_link_conflicts():
    args = SolverParams().to_args()
    assert args[args.index("--method") + 1] == "bp"
    assert "--no-link-conflicts" not in args
    assert "--lns-frac" not in args


def test_original_model_flag():
    assert "--no-link-conflicts" in SolverParams(link_conflicts=False).to_args()


def test_lns_flags():
    p = SolverParams(method="lns", lns_init_frac=0.2, lns_operators=["dual", "rejected"], lns_bound_pruning=False, seed=7)
    args = p.to_args()
    get = lambda f: args[args.index(f) + 1]
    assert get("--method") == "lns"
    assert float(get("--lns-frac")) == 0.2
    assert get("--lns-ops") == "dual,rejected"
    assert get("--seed") == "7"
    assert "--lns-no-prune" in args


def test_alns_flags():
    p = SolverParams(method="lns", lns_repairs=["greedy", "dive_mip"], lns_accept="improve",
                     lns_operators=["bottleneck"], lns_segment=20)
    args = p.to_args()
    get = lambda f: args[args.index(f) + 1]
    assert get("--lns-repairs") == "greedy,dive_mip"
    assert get("--lns-accept") == "improve"
    assert get("--lns-ops") == "bottleneck"
    assert get("--lns-segment") == "20"
    with pytest.raises(ValidationError):
        SolverParams(method="lns", lns_repairs=[])
    with pytest.raises(ValidationError):
        SolverParams(method="lns", lns_accept="always")


def test_invalid_lns_settings_rejected():
    with pytest.raises(ValidationError):
        SolverParams(method="lns", lns_operators=[])
    with pytest.raises(ValidationError):
        SolverParams(method="lns", lns_operators=["nope"])
    with pytest.raises(ValidationError):
        SolverParams(method="other")
