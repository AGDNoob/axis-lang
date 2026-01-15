# AXIS Stress Test Suite Summary

Write-Host "===================================" -ForegroundColor Cyan
Write-Host "AXIS Stress Test Results" -ForegroundColor Cyan
Write-Host "===================================" -ForegroundColor Cyan
Write-Host ""

$tests = @(
    "stress_test_bool",
    "stress_test_negation",
    "stress_test_mixed",
    "stress_test_comments",
    "stress_test_loop_bool",
    "stress_test_extreme",
    "stress_test_edge_cases",
    "stress_test_realistic"
)

$passed = 0
$failed = 0

foreach ($test in $tests) {
    $source = "tests\$test.axis"
    $output = "tests\$test"
    
    Write-Host "Testing: $test" -NoNewline
    
    # Compile
    $compileResult = & python compilation_pipeline.py $source -o $output --elf 2>&1
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host " [COMPILE FAILED]" -ForegroundColor Red
        $failed++
        Write-Host "  Error: $compileResult" -ForegroundColor Red
        continue
    }
    
    Write-Host " [COMPILED]" -ForegroundColor Green -NoNewline
    
    # Extract binary size
    if ($compileResult -match "Total: (\d+) bytes") {
        $size = $Matches[1]
        Write-Host " ($size bytes)" -ForegroundColor Gray
    } else {
        Write-Host ""
    }
    
    $passed++
}

Write-Host ""
Write-Host "===================================" -ForegroundColor Cyan
Write-Host "Summary:" -ForegroundColor Cyan
Write-Host "  Passed: $passed" -ForegroundColor Green
Write-Host "  Failed: $failed" -ForegroundColor $(if ($failed -gt 0) { "Red" } else { "Green" })
Write-Host "===================================" -ForegroundColor Cyan
