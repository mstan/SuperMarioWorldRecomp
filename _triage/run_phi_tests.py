import sys, pathlib, importlib
REPO = pathlib.Path('snesrecomp/tests')
sys.path.insert(0, 'snesrecomp/recompiler')
sys.path.insert(0, str(REPO))

for name in ['test_phi_merge_jmp_back_edge',
             'test_phi_merge_x_param',
             'test_diagonal_ledge_jmp_phi',
             'test_tail_call_x_restore',
             'test_promote_rety_loop_thread',
             'test_phi_prealloc_conditional_branch']:
    m = importlib.import_module(name)
    fns = [getattr(m, n) for n in dir(m) if n.startswith('test_')]
    for fn in fns:
        try:
            fn()
            print(f'PASS {name}.{fn.__name__}')
        except AssertionError as e:
            print(f'FAIL {name}.{fn.__name__}: {str(e)[:200]}')
        except Exception as e:
            print(f'ERR  {name}.{fn.__name__}: {type(e).__name__}: {str(e)[:200]}')
