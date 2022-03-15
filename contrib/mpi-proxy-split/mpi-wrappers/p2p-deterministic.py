#!/usr/bin/python
import sys
import os

def macroize_line(line):
  if "//" in line:
    line = line.replace("//", "/*", 1) + " */"
  n = max(0, 60 - len(line))
  return line + n*" " + " \\"

def macroize(lines):
  in_macro = False
  out = ["// *** THIS FILE IS AUTO-GENERATED! DO 'make' TO UPDATE. ***", ""]
  lines = [line.rstrip() for line in lines]
  for line in lines:
    if not in_macro and line.startswith("#define ") and "(" in line:
      in_macro = True
      out.append(macroize_line(line))
    elif not in_macro:
      out.append(line)
    elif in_macro and line.strip(): # if non-empty line of macro
      out.append(macroize_line(line))
    elif in_macro and not line.strip(): # if empty line of macro: end macro
      out[-1] = out[-1][:-2].rstrip()  # Strip final " \\"
      out.append("")
      in_macro = False;
  return "\n".join(out)

if len(sys.argv) == 2 and os.path.isfile(sys.argv[1]) and \
   os.access(sys.argv[1], os.R_OK):
  print(macroize(open(sys.argv[1]).readlines()))
else:
  sys.stderr.write("macroize: Can't macroize; no such file or not readable\n" +
                   "USAGE: macroize.py FILE\n")
