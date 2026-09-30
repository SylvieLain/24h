# 当前任务

2026-09-30：任务3 B 点第二次出弯仍转得太慢，用户要求立刻转弯、车体持续前进。当前B目标仍为出线Z角+50°+TASK3_B_AMEND，用户设TASK3_B_AMEND=-0.0°；其他角度与循迹参数保持用户现值。main.c 当前用户已把弧线基础PWM调到3500，本次没有改main.c。

本次仅改source/app/tasks/task.c：B出弯目标切换后立即执行首拍PD；仅TASK3且car_pos=B时比例转向额外加倍，差量限±3000；对外侧轮按差量连续补速，满差量时内侧0/外侧9999，归零后两侧3000。B→D仍由Z角PD更新；没有加状态位或改其他任务。原出弯版满差量为0/6000，本次改变是外侧轮到9999。

EIDE增量编译、链接、生成HEX成功；git diff --check无错误。原有post-build cp在Windows失败、RWX警告仍在。dap flash --elf build\Debug\9.5.elf --json 返回 ok=true、verified=true、server_restarted=true、target_running=true；ELF SHA256=c6be4fd99355c81cc9a0177a26de33cacf63d83f5232145152b1daaae8d9e4ce。当前无实车角度/落点验证；下一步记录B出线后到达到目标角的时间、位移以及B→D入线结果。满速单向差速仍不能保证几何位置，若不够则需要实测决定是否提前触发/调整目标。
