# audit_v1_directive_strip — report

v2 cfg_loader ignores all of these directives.
Tokens stripped from `func`/`name` lines; whole-line
`default_init_y = ...` deletes counted separately.

| Bank | Lines mutated | default_init_y delete | init_y | init_carry | restores_x | y_after | x_after | carry_ret | ret_y |
|---|---|---|---|---|---|---|---|---|---|
| bank00.cfg | 2 | 0 | 0 | 1 | 0 | 0 | 0 | 0 | 1 |
| bank01.cfg | 4 | 1 | 0 | 0 | 0 | 0 | 0 | 2 | 1 |
| bank02.cfg | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 0 |
| bank03.cfg | 5 | 0 | 0 | 0 | 0 | 0 | 0 | 2 | 3 |
| bank04.cfg | 5 | 0 | 0 | 0 | 1 | 2 | 0 | 1 | 1 |
| bank05.cfg | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| bank07.cfg | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| bank0c.cfg | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| bank0d.cfg | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 2 |
| **TOTAL** | **19** | **1** | **0** | **1** | **1** | **2** | **0** | **6** | **8** |
