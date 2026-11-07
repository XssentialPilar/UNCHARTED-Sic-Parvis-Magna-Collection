import unittest
from analyze_performance_log import analyze


class PerformanceLogTests(unittest.TestCase):
    def test_memory_growth_and_recycling_are_distinct_from_physical_release(self):
        lines = [
            'Memory: RAM 5000 MiB private, 3000 MiB working set; video memory 2000 of 7000 MiB '
            '(driver); buffer arenas 1000 MiB (900 in use, 0 given back so far), images 500 MiB (20)',
            'GPU: buffer arenas gave back 451 MiB',
            'Memory: RAM 5200 MiB private, 3200 MiB working set; video memory 2100 of 7000 MiB '
            '(driver); buffer arenas 1000 MiB (449 in use, 451 given back so far), images 600 MiB (30)',
        ]
        result = analyze(iter(lines))
        self.assertEqual(result['memory_samples'], 2)
        self.assertEqual(result['arena_recycle_reports'], 1)
        self.assertEqual(result['arena_physical_trim_reports'], 0)
        self.assertEqual(result['last_memory']['arena_in_use_mib'], 449)
        self.assertEqual(result['peak_memory']['arena_allocated_mib'], 1000)
        self.assertEqual(result['peak_vram_sample']['line'], 3)
        self.assertEqual(result['vram_samples_above_budget'], 0)

    def test_missing_statistics_and_device_loss_do_not_imply_oom(self):
        result = analyze(['unrelated line', 'ErrorDeviceLost', 'STOP: GPU assertion'])
        self.assertNotIn('peak_memory', result)
        self.assertIsNone(result['median_logged_fps'])
        self.assertEqual(result['fatal_diagnostics'][0]['line'], 2)

    def test_frame_statistics_are_log_samples(self):
        result = analyze(['Frame stats: 60.0 FPS', 'Frame stats: 120.0 FPS', 'Stall: 150.5 ms frame',
                          'Frame pacing: median 16.67 ms, stddev 2.00 ms, p99 28.00 ms, '
                          '12 frames over 1.5x median'])
        self.assertEqual(result['median_logged_fps'], 90)
        self.assertEqual(result['minimum_logged_fps'], 60)
        self.assertEqual(result['max_reported_stall_ms'], 150.5)
        self.assertEqual(result['median_window_frame_ms'], 16.67)
        self.assertEqual(result['worst_window_p99_ms'], 28)


if __name__ == '__main__':
    unittest.main()
