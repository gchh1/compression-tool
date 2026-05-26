@echo off
python _test_run3.py > _test_stdout.txt 2> _test_stderr.txt
echo Exit code: %ERRORLEVEL% > _test_exit.txt
type _test_stdout.txt
type _test_stderr.txt
type _test_result.txt 2>nul