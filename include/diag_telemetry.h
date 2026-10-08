#pragma once

/**
 * Starts the periodic background diagnostic telemetry task on Core 0.
 * Logs heap, stack watermarks, queue occupancies, and network stats every 15 seconds.
 */
void diag_telemetry_start();
