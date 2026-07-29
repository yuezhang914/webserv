#!/usr/bin/env python3

import os

print("Content-Type: text/plain")
print()

print(os.environ.get("QUERY_STRING"))