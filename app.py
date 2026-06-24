import streamlit as st
st.set_page_config(layout="wide")

import matplotlib.pyplot as plt
import pandas as pd
import json
import os
import subprocess

#EXAMPLES_DIR = "examples"


PIN_PATH = "/home/avidhaa/pin_kit/pin"

TOOL_PATH = "/home/avidhaa/pin_kit/source/tools/MyPinTool/obj-intel64/MyPinTool.so"

EXAMPLES_DIR = "tests"
TESTS_DIR = "examples"

POLICIES = ["LRU", "FIFO", "LFU"]
COLORS = {"LRU": "#185FA5", "FIFO": "#0F6E56", "LFU": "#993C1D"}
CACHE_SIZE = 8

for key, default in [
    ("player_idx", 0),
    ("player_cache", []),
    ("player_hits", 0),
]:
    if key not in st.session_state:
        st.session_state[key] = default

st.sidebar.header("⚙️ Compile C/C++ Program")

source_files = []
if os.path.exists(TESTS_DIR):
    source_files = [
        f for f in os.listdir(TESTS_DIR)
        if f.endswith((".c", ".cc", ".cpp"))
        and os.path.isfile(os.path.join(TESTS_DIR, f))
    ]

if source_files:
    src = st.sidebar.selectbox("Source file", source_files, key="compile_source")
    out_name = st.sidebar.text_input("Output name", value=os.path.splitext(src)[0])

    if st.sidebar.button("Compile"):
        os.makedirs(EXAMPLES_DIR, exist_ok=True)
        compiler = "gcc" if src.endswith(".c") else "g++"
        cmd = [compiler, "-O1", os.path.join(TESTS_DIR, src), "-o",
               os.path.join(EXAMPLES_DIR, out_name)]
        with st.spinner("Compiling…"):
            res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            st.sidebar.error("Compilation failed")
            st.sidebar.code(res.stderr)
        else:
            st.sidebar.success(f"✓ Compiled → examples/{out_name}")
            st.rerun()
else:
    st.sidebar.info("No source files found in tests/")

st.sidebar.divider()
st.sidebar.header("▶ Run PIN Tool")

examples = []
if os.path.exists(EXAMPLES_DIR):
    examples = [
        f for f in os.listdir(EXAMPLES_DIR)
        if os.path.isfile(os.path.join(EXAMPLES_DIR, f))
        and os.access(os.path.join(EXAMPLES_DIR, f), os.X_OK)
    ]

if examples:
    selected = st.sidebar.selectbox("Choose program", examples)
    if st.sidebar.button("Run simulation"):
        with st.spinner("Running PIN tool…"):
            cmd = [PIN_PATH, "-t", TOOL_PATH, "--",
                   os.path.join(EXAMPLES_DIR, selected)]
            res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            st.sidebar.error("Simulation failed")
            st.sidebar.code(res.stderr)
        else:
            st.sidebar.success("✓ Simulation complete")
            st.rerun()
else:
    st.sidebar.info("No executables found in examples/")

summary = None
trace = pd.DataFrame()

if os.path.exists("UIoutput.json"):
    with open("UIoutput.json") as f:
        summary = json.load(f)

if os.path.exists("memtrace.ndjson"):
    try:
        trace = pd.read_json("memtrace.ndjson", lines=True)
    except Exception:
        trace = pd.DataFrame()

st.title("Cache Simulator Dashboard")
st.caption("LRU · FIFO · LFU")

if summary is None and trace.empty:
    st.info("Run the PIN tool (or place `UIoutput.json` / `memtrace.ndjson` in this directory) to see results.")
    st.stop()

cfg = summary.get("config {}") if summary else None
if cfg:
    st.sidebar.divider()
    st.sidebar.header("📐 Cache Config")
    st.sidebar.write(f"**Size:** {cfg['cache_size']:,} bytes ({cfg['cache_size']//1024} KB)")
    st.sidebar.write(f"**Block:** {cfg['block_size']} bytes")
    st.sidebar.write(f"**Associativity:** {cfg['associativity']}")
    st.sidebar.write(f"**Sets:** {cfg['num_sets']}")

tab_overview, tab_trace, tab_miss = st.tabs(
    ["📊 Overview", "🔍 Raw Trace", "❌ Miss Analysis"]
)


with tab_overview:
    if summary is None:
        st.warning("UIoutput.json not found.")
    else:
        total = (summary[POLICIES[0]]["hits"] + summary[POLICIES[0]]["misses"]) or 1
        hit_rates = {p: summary[p]["hits"] / total for p in POLICIES}
        best = max(hit_rates, key=hit_rates.get)

        st.subheader("Summary")
        cols = st.columns(len(POLICIES))
        for i, p in enumerate(POLICIES):
            h = summary[p]["hits"]
            m = summary[p]["misses"]
            t = h + m or 1
            with cols[i]:
                label = f"{'🏆 ' if p == best else ''}{p}"
                st.metric(label, f"{h/t:.4f}", help=f"Hits: {h:,} · Misses: {m:,}")
                st.progress(h / t)

        st.divider()

        st.subheader("Hit / Miss breakdown")
        fig, axes = plt.subplots(1, 3, figsize=(12, 4))
        for ax, p in zip(axes, POLICIES):
            h = summary[p]["hits"]
            m = summary[p]["misses"]
            wedge_colors = [COLORS[p], "#d1d5db"]
            ax.pie([h, m], labels=["Hits", "Misses"], autopct="%1.1f%%",
                   colors=wedge_colors, startangle=90,
                   wedgeprops={"linewidth": 0.5, "edgecolor": "white"})
            title = f"{p}" + (" ★ best" if p == best else "")
            ax.set_title(title, fontweight="bold" if p == best else "normal")
        fig.tight_layout()
        st.pyplot(fig)
        plt.close(fig)

        if not trace.empty:
            st.divider()
            st.subheader("Hit rate over time")
            _ov_max = len(trace)
            _ov_min = min(100, _ov_max)
            _ov_default = min(5_000, _ov_max)
            if _ov_min < _ov_max:
                limit = st.slider("Trace entries to plot", _ov_min, _ov_max, _ov_default, key="ov_slider")
            else:
                limit = _ov_max
                st.caption(f"Plotting all {_ov_max} trace entries")
            t_slice = trace.head(limit).copy()

            fig2, ax2 = plt.subplots(figsize=(12, 4))
            for p in POLICIES:
                col = f"hit{p}"
                if col in t_slice.columns:
                    cumrate = t_slice[col].astype(int).cumsum() / (pd.RangeIndex(len(t_slice)) + 1)
                    ax2.plot(cumrate.values, label=p, color=COLORS[p], linewidth=1.5)
            ax2.set_title("Cumulative hit rate")
            ax2.set_xlabel("Memory access #")
            ax2.set_ylabel("Hit rate")
            ax2.set_ylim(0, 1)
            ax2.legend()
            ax2.grid(True, alpha=0.3)
            fig2.tight_layout()
            st.pyplot(fig2)
            plt.close(fig2)

with tab_trace:
    if trace.empty:
        st.warning("No trace data available.")
    else:
        col_a, col_b = st.columns([3, 1])
        with col_a:
            _tr_max = len(trace)
            _tr_min = min(100, _tr_max)
            _tr_default = min(5_000, _tr_max)
            if _tr_min < _tr_max:
                limit = st.slider("Entries to load", _tr_min, _tr_max, _tr_default, key="trace_slider")
            else:
                limit = _tr_max
                st.caption(f"Showing all {_tr_max} entries")
        with col_b:
            diff_cols = [f"hit{p}" for p in POLICIES if f"hit{p}" in trace.columns]
            show_diff = st.checkbox("Divergence only", value=False)

        t_view = trace.head(limit).copy()
        if show_diff and diff_cols:
            mask = t_view[diff_cols].nunique(axis=1) > 1
            t_view = t_view[mask]
            st.caption(f"Showing {len(t_view):,} rows where policies disagree (of {limit:,} loaded)")
        else:
            st.caption(f"Showing {len(t_view):,} rows")

        st.dataframe(t_view, use_container_width=True)


with tab_miss:
    if trace.empty or summary is None:
        st.warning("Need both UIoutput.json and memtrace.ndjson.")
    else:
        st.subheader("Miss count per policy")
        total = (summary[POLICIES[0]]["hits"] + summary[POLICIES[0]]["misses"]) or 1

        fig, ax = plt.subplots(figsize=(7, 4))
        miss_vals = [summary[p]["misses"] for p in POLICIES]
        bars = ax.bar(POLICIES, miss_vals, color=[COLORS[p] for p in POLICIES],
                      edgecolor="white", linewidth=0.5)
        for bar, val in zip(bars, miss_vals):
            ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + total * 0.002,
                    f"{val:,}", ha="center", va="bottom", fontsize=11)
        ax.set_ylabel("Miss count")
        ax.set_title("Total misses by replacement policy")
        ax.grid(True, axis="y", alpha=0.3)
        ax.spines[["top", "right"]].set_visible(False)
        fig.tight_layout()
        st.pyplot(fig)
        plt.close(fig)

        # Miss rate comparison table
        st.subheader("Policy metrics table")
        rows = []
        for p in POLICIES:
            h = summary[p]["hits"]
            m = summary[p]["misses"]
            t = h + m or 1
            rows.append({
                "Policy": p,
                "Hits": f"{h:,}",
                "Misses": f"{m:,}",
                "Hit rate": f"{h/t:.4f}",
                "Miss rate": f"{m/t:.4f}",
                "Best?": "✓" if p == max(POLICIES, key=lambda x: summary[x]["hits"]) else "",
            })
        st.dataframe(pd.DataFrame(rows), use_container_width=True, hide_index=True)

        # Divergence in trace
        if not trace.empty:
            diff_cols = [f"hit{p}" for p in POLICIES if f"hit{p}" in trace.columns]
            if diff_cols:
                st.subheader("Trace divergence")
                diverged = trace[trace[diff_cols].nunique(axis=1) > 1]
                st.metric("Accesses where policies disagree", f"{len(diverged):,}")
                st.dataframe(diverged.head(100), use_container_width=True)
