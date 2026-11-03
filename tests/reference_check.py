"""Independent Python standard-library numerical reference for the C++ CLI fixture."""
import csv
import math
import re
import subprocess
import sys
from pathlib import Path

exe = Path(sys.argv[1])
fixture = Path(sys.argv[2])
rows = list(csv.DictReader(fixture.open(newline="", encoding="utf-8")))


def expected_metrics(weights):
    dates = sorted({row["trade_date"] for row in rows})
    values, spy = [], []
    for date in dates:
        day = {r["ticker"]: r for r in rows if r["trade_date"] == date}
        if not all(t in day for t in weights):
            continue
        values.append(sum(w * float(day[t]["sector_return"]) for t, w in weights.items()))
        spy.append(float(next(iter(day.values()))["market_return"]))

    def risk(numbers):
        avg = sum(numbers) / len(numbers)
        vol = math.sqrt(sum((x - avg) ** 2 for x in numbers) / (len(numbers) - 1) * 252) * 100
        losses = sorted([-x for x in numbers])
        var = losses[math.ceil(.95 * len(losses)) - 1] * 100
        worst = sorted(losses, reverse=True)[:math.ceil(.05 * len(losses))]
        es = sum(worst) / len(worst) * 100
        wealth, peak, drawdown = 1.0, 1.0, 0.0
        for ret in numbers:
            wealth *= 1 + ret
            peak = max(peak, wealth)
            drawdown = max(drawdown, 1 - wealth / peak)
        return [vol, avg * 252 * 100, var, es, drawdown * 100, min(numbers) * 100]

    return risk(values), risk(spy)


def run(weights, args):
    output = subprocess.check_output(
        [str(exe), "--data", str(fixture), *args], text=True, stderr=subprocess.STDOUT
    )
    p, spy = expected_metrics(weights)
    names = ["annualized volatility", "annualized arithmetic mean", "historical VaR",
             "historical ES", "maximum drawdown \\(loss\\)", "worst daily return"]
    for label, values in [("Portfolio", p), ("SPY", spy)]:
        for name, expected in zip(names, values):
            found = re.search(rf"^{label} {name}: ([-+0-9.eE]+)%$", output, re.M)
            if not found:
                raise AssertionError(f"Missing {label} {name} in CLI output")
            actual = float(found.group(1))
            if abs(actual - expected) > .000051:
                raise AssertionError(f"{label} {name}: C++ {actual}, Python {expected}")
    if "Status: PASSED" not in output:
        raise AssertionError("Matrix variance validation did not pass")


run({"XLK": .4, "XLE": .3, "XLP": .3}, [])
run({"XLK": .25, "XLE": .75}, ["XLK=0.25", "XLE=0.75"])
print("Independent Python reference checks passed for default and custom allocations.")
