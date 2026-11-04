#!/bin/sh
exec "$(dirname "$0")/../run_policy_test.sh" bounded_history BoundedHistoryPolicyTest.cpp
