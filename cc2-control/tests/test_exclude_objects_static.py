#!/usr/bin/env python3
"""Regression checks for large EXCLUDE_OBJECT workloads."""

from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / "src" / "main.c"
text = SOURCE.read_text(encoding="utf-8")

assert '#define EXCLUDE_OBJECT_RESPONSE_MAX (256UL * 1024UL)' in text
assert 'exclude_object\\\":[\\\"objects\\\"]' in text
assert 'exclude_object\\\":[\\\"excluded_objects\\\",\\\"current_object\\\"]' in text
start = text.index("static void exclude_objects_response(")
end = text.index("\\nstatic void mesh_response(", start)
exclude_handler = text[start:end]
assert 'char *result=malloc(65537)' not in exclude_handler
assert 'used<65536' not in exclude_handler
assert 'realloc(result, next + 1)' in text
assert 'strcmp(exclude_objects_cache_job, job)' in text
assert 'exclude_objects_response(fd, mqtt);' in text

print("exclude-object resource-pressure regression checks passed")
