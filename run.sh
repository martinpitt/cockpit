#!/bin/sh -e
TEST_OS=rhel-8-0 test/verify/check-realms TestRealms.testClientCertAuthentication -stv
