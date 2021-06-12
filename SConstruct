import os
import subprocess

# FIXME:  Decider('MD5-timestamp') doesn't seem to work as expected.
Decider('timestamp-match')

ENV = os.environ
Export("ENV")

# Define constructor var. (e.g., CFLAGS) if defined in the Linux environment.
env = Environment()
_os_environ = dict(os.environ)
for key, value in _os_environ.iteritems():
  if "FLAG" in key:
    _os_environ[key] = value.split()
env.Dictionary().update(_os_environ)  # Bring OS environment into env.

# Get any assignments to environment variables in config.log
# For example:  ./configure CFLAGS="-g3 -O0"
defines1 = subprocess.Popen(["grep", "'  $ ./configure'", "config.log"],
                           stdout=subprocess.PIPE).communicate()[0]
defines2 = [arg.split("=") for arg in defines1.split() if "=" in arg]
    
cppdefines = []
for key, value in ARGLIST + defines2:
  if key.startswith("-D"):
    cppdefines.append(key[2:] + "=" + value)
  elif "FLAG" in key:  # overwrite environment value with configure value
    env[key] = value.split()
  else:
    env[key] = value   # overwrite environment value with configure value
env.Append(CPPDEFINES = cppdefines)

Export("env")

SConscript("contrib/mpi-proxy-split/SConscript")
