# -*- coding: utf-8 -*-
# 历史直立环 P 扫参复盘图；新采集统一使用根目录 dap CLI。
# 数据: 53Hz OpenOCD 采样 CSV, 列: time_ms,enabled,output,kp_q10,angle_x,...
# angle_x 原始量换算: deg = raw * 180 / 32768
import os
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams["font.sans-serif"] = ["Microsoft YaHei", "SimHei"]
plt.rcParams["axes.unicode_minus"] = False

SRC = r"C:\Windows\TEMP\opencode"
OUT = r"D:\MCU_Code\32hal\balancd_car\Tool\OpenOCD\plots"
os.makedirs(OUT, exist_ok=True)

SPEC = [  # (文件, kp_q10)
    ("step768.csv", 768), ("step1024.csv", 1024), ("step1280.csv", 1280),
    ("step1536.csv", 1536), ("step1792.csv", 1792), ("step2048.csv", 2048),
    ("s2_2048.csv", 2048), ("s2_2560.csv", 2560), ("s2_3072.csv", 3072),
    ("s2_3584.csv", 3584),
]

rows = []
for fname, kp in SPEC:
    path = os.path.join(SRC, fname)
    if not os.path.exists(path):
        continue
    df = pd.read_csv(path)
    df["angle_deg"] = df["angle_x"] * 180.0 / 32768.0
    df["t_s"] = df["time_ms"] / 1000.0
    dev = (df["angle_deg"] - df["angle_deg"].median()).abs()
    d = (df["angle_deg"] - df["angle_deg"].median()).map(lambda v: 1 if v >= 0 else -1)
    zc = (d != d.shift()).sum() / 2.0 / 8.0
    rows.append({
        "kp": kp, "df": df, "devAvg": dev.mean() * 100,  # 单位 0.01 度, 与 MATLAB 版一致
        "zc": zc,
        "outMax": int(df["output"].abs().max()),
        "sat": 100.0 * (df["output"].abs() >= 4950).mean(),
    })
    print(f"kp={kp:5d}  devAvg={dev.mean()*100:7.1f}e-2deg  zc={zc:4.2f}Hz  "
          f"outMax={rows[-1]['outMax']:5d}  sat={rows[-1]['sat']:5.1f}%  rows={len(df)}")

# ---- 图1: 各档 angle_x 轨迹 ----
fig, axes = plt.subplots(3, 4, figsize=(16, 9), sharey=True)
fig.suptitle("直立环 P 扫参：angle_x 轨迹（扶车扰动/松手混合）", fontsize=14)
for i, r in enumerate(rows):
    ax = axes[i // 4][i % 4]
    ax.plot(r["df"]["t_s"], r["df"]["angle_deg"], lw=0.8, color="#1f6fb5")
    ax.axhline(0, ls=":", c="gray", lw=0.8)
    ax.set_ylim(-25, 25)
    ax.set_title(f"kp_q10={r['kp']} (Kp={r['kp']/1024:.2f})", fontsize=10)
    ax.set_xlabel("s"); ax.set_ylabel("deg")
for j in range(len(rows), 12):
    axes[j // 4][j % 4].axis("off")
fig.tight_layout(rect=[0, 0, 1, 0.95])
fig.savefig(os.path.join(OUT, "fig1_traces.png"), dpi=150)
plt.close(fig)

# ---- 图2: 指标 vs kp，标注失稳边界 ----
fig, (a1, a2) = plt.subplots(2, 1, figsize=(9, 6), sharex=True)
a1.bar([r["kp"] for r in rows], [r["devAvg"] for r in rows], color="#2e8b57")
a1.axvline(3200, color="r", ls="--"); a1.grid(True, axis="y")
a1.set_ylabel("平均偏差 (0.01°)")
a1.set_title("devAvg vs kp —— 2560 最优，3584 失稳")
a2.bar([r["kp"] for r in rows], [r["zc"] for r in rows], color="#b5651d")
a2.axvline(3200, color="r", ls="--"); a2.grid(True, axis="y")
a2.set_ylabel("过零率 (Hz)"); a2.set_xlabel("kp_q10")
a2.set_title("振荡前兆：过零率随 kp 上升")
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig2_metrics.png"), dpi=150)
plt.close(fig)

# ---- 图3: 正反教材 ----
g = next(r for r in rows if r["kp"] == 2560)
b = next(r for r in rows if r["kp"] == 3584)
fig, (a1, a2) = plt.subplots(2, 1, figsize=(11, 7))
a1.plot(g["df"]["t_s"], g["df"]["angle_deg"], lw=1, color="#1f6fb5")
a1.axhline(0, ls=":", c="gray"); a1.grid(True)
a1.set_ylabel("deg")
a1.set_title(f"松手稳态 kp_q10=2560：devAvg={g['devAvg']:.0f}e-2°，输出峰值 {g['outMax']}")
a2.grid(True)
a2.plot(b["df"]["t_s"], b["df"]["angle_deg"], lw=1, color="#1f6fb5", label="angle_x (deg)")
a2.set_ylabel("deg", color="#1f6fb5")
a3 = a2.twinx()
a3.plot(b["df"]["t_s"], b["df"]["output"], lw=0.7, color="r", alpha=0.7, label="output")
a3.axhline(5000, ls="--", c="r"); a3.axhline(-5000, ls="--", c="r")
a3.set_ylabel("output", color="r")
a2.set_xlabel("s")
ln1, lb1 = a2.get_legend_handles_labels()
ln2, lb2 = a3.get_legend_handles_labels()
a2.legend(ln1 + ln2, lb1 + lb2, loc="upper right", fontsize=9)
a2.set_title(f"极限环失稳 kp_q10=3584：输出 {b['sat']:.0f}% 时间饱和 → Key3 中止")
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig3_good_vs_bad.png"), dpi=150)
plt.close(fig)

print("图已保存到", OUT)
