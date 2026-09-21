% 历史直立环 P 扫参复盘图；新采集统一使用根目录 dap CLI。
% 数据源: C:\Windows\TEMP\opencode\*.csv (53Hz OpenOCD 采样, 列: time_ms,enabled,output,kp_q10,angle_x,...)
% angle_x 原始量, 换算: deg = raw * 180 / 32768
clear; clc; close all;

src = 'C:\Windows\TEMP\opencode\';
outdir = 'D:\MCU_Code\32hal\balancd_car\Tool\OpenOCD\plots\';
if ~exist(outdir, 'dir'); mkdir(outdir); end

files = {'step768.csv','step1024.csv','step1280.csv','step1536.csv','step1792.csv', ...
         'step2048.csv','s2_2048.csv','s2_2560.csv','s2_3072.csv','s2_3584.csv'};
kps   = [768 1024 1280 1536 1792 2048 2048 2560 3072 3584];

n = 0; T = {}; KP = []; DEV = []; ZC = []; OUTMAX = []; SAT = [];
for i = 1:numel(files)
    f = fullfile(src, files{i});
    if ~exist(f, 'file'); continue; end
    T{i} = readtable(f);
    n = n + 1;
    kp = kps(i);
    ax = T{i}.angle_x * 180/32768;
    med = median(ax);
    dev = abs(ax - med);
    DEV(n,1) = mean(dev);
    KP(n,1)  = kp;
    d = sign(ax - med); zc = sum(d(2:end) ~= d(1:end-1));
    ZC(n,1)  = zc/2/8.0;                       % 过零率 Hz
    OUTMAX(n,1) = max(abs(T{i}.output));
    SAT(n,1) = 100 * sum(abs(T{i}.output) >= 4950) / height(T{i});
    fprintf('kp=%5d  devAvg=%6.1f deg*1e-2?  zc=%4.2f Hz  outMax=%5d  sat=%5.1f%%  rows=%d\n', ...
        kp, DEV(n)*100, ZC(n), OUTMAX(n), SAT(n), height(T{i}));
end

% ---- 图1: 各档 angle_x 轨迹 ----
figure('Name','P扫参轨迹','Position',[50 50 1200 800]);
for i = 1:n
    subplot(3, 4, i); hold on; grid on;
    t = T{i}.time_ms/1000;
    ax = T{i}.angle_x * 180/32768;
    plot(t, ax, 'LineWidth', 0.8);
    yline(0, ':');
    ylabel('\circ'); xlabel('s');
    title(sprintf('kp_{q10}=%d (Kp=%.2f)', KP(i), KP(i)/1024));
    ylim([-25 25]);
end
sgtitle('直立环 P 扫参: angle_x 轨迹 (你扶车扰动/松手)');
print(fullfile(outdir,'fig1_traces.png'), '-dpng', '-r150');

% ---- 图2: 指标 vs kp, 标注稳定边界 ----
figure('Name','指标对比','Position',[100 100 900 600]);
subplot(2,1,1);
bar(KP, DEV*100); grid on; hold on;
xline(3200, 'r--', 'LineWidth', 1.5);          % 失稳边界区
ylabel('平均偏差 (0.01\circ)');
title(sprintf('devAvg vs kp  (推荐 kp_{q10}=%d)', 2560));
subplot(2,1,2);
bar(KP, ZC); grid on; hold on;
xline(3200, 'r--', 'LineWidth', 1.5);
ylabel('过零率 (Hz)'); xlabel('kp_{q10}');
title('振荡前兆: 过零率随 kp 上升');
print(fullfile(outdir,'fig2_metrics.png'), '-dpng', '-r150');

% ---- 图3: 正反教材 2560稳态 vs 3584失稳 ----
iG = find(KP == 2560, 1); iB = find(KP == 3584, 1);
figure('Name','正反教材','Position',[150 150 1100 700]);
subplot(2,1,1); hold on; grid on;
tg = T{iG}.time_ms/1000; ag = T{iG}.angle_x * 180/32768;
plot(tg, ag, 'b', 'LineWidth', 1);
yline(0, ':');
ylabel('\circ');
title(sprintf('松手稳态: kp_{q10}=2560, devAvg=%.2f^\\circ, 输出峰值%d', DEV(iG), OUTMAX(iG)));
subplot(2,1,2); hold on; grid on;
tb = T{iB}.time_ms/1000; ab = T{iB}.angle_x * 180/32768;
yyaxis left;  plot(tb, ab, 'b', 'LineWidth', 1); ylabel('\circ');
yyaxis right; plot(tb, T{iB}.output, 'r', 'LineWidth', 0.7); ylabel('output');
yline(5000, 'r--'); yline(-5000, 'r--');
title(sprintf('极限环失稳: kp_{q10}=3584, 输出 %.0f%% 时间饱和 → 你按 Key3', SAT(iB)));
print(fullfile(outdir,'fig3_good_vs_bad.png'), '-dpng', '-r150');

fprintf('图已保存到 %s\n', outdir);
