/*
 * dp_sync_bridge.c — vestigial bridge stubs
 *
 * The original bridge maintained C-pointer mirrors of WRAM bytes
 * ($6B/$6C → ptr_lo_map16_data, $04/$05 → ptr_lo_map16_data_bak)
 * for an oracle layer that no longer exists. Nothing reads those
 * pointers anymore.
 *
 * The functions remain as no-op stubs because the recompiler still
 * emits calls to them at oracle/generated code boundaries. Once the
 * recompiler is taught to stop emitting these calls, this file can
 * be deleted entirely along with the related cfg name/sig overrides.
 */

void dp_sync_map16_ptr(void)        { }
void dp_sync_map16_ptr_bak(void)    { }
void dp_sync_map16_ptr_to_dp(void)  { }
