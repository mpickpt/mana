#!/usr/bin/env python

# This Python script is used for creating shadow library for MPI-executables
#   linked with libraries containing constructors at comile time.
# This Python script will create a dummy library symbolic links 
#   with same name as dependencies, to avoid loader errors in UH 

import os
import sys
import subprocess
import tempfile

DELIMITED_SHADOW_LIB_KEYWORDS = ["intel", "ucx"]
INLINE_SHADOW_LIB_KEYWORDS = ["-intel2"]    # example: `4.0.1-intel2022`

#------------------------------check for library dependencies tree-------------------------------
def filter_library_dependencies(executable):
  """
    Runs ldd on the executable and extracts 
    symbolic and absolute library names.
  """
  try :
    result = subprocess.run(["ldd", executable], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
  except subprocess.CalledProcessError:
    print(f"Error: UNable to run ldd on {executable}")
    sys.exit(1)

  global DELIMITED_SHADOW_LIB_KEYWORDS
  global INLINE_SHADOW_LIB_KEYWORDS

  filtered_dependencies = set()   # using set instead of list to remove duplicates
  
  # Filter and preprocess lines that contain ' => '
  lines = [line for line in result.stdout.splitlines() if " => " in line]

  for line in lines:
    parts = line.split(" => ")

    # Ensure a valid library mapping
    if len(parts) == 2 and "not found" not in parts[1]:
      symbolic_name = parts[0].strip()
      absolute_path = parts[1].split()[0] # Extract absolute path before any extra text
      
      # Check if absolute_path contains any keyword from DELIMITED_SHADOW_LIB_KEYWORDS 
      # EXAMPLE: parses out 'libsvml.so' due to presence of '/ucx/'
      #          libucm.so.0 => /shared/centos7/ucx/1.10.1-intel2021/lib/libucm.so.0
      if any(f'/{keyword}/' in absolute_path for keyword in DELIMITED_SHADOW_LIB_KEYWORDS):
        filtered_dependencies.add(symbolic_name)
      
      # Check if absolute_path contains any keyword from INLINE_SHADOW_LIB_KEYWORDS
      # EXAMPLE: parses out 'limpi.so.12' due to presence of '-intel'
      #          libmpi.so.12 => /shared/centos7/mpich/4.0.1-intel2022/lib/libmpi.so.12
      elif any(f'{keyword}' in absolute_path for keyword in INLINE_SHADOW_LIB_KEYWORDS):
        filtered_dependencies.add(symbolic_name)
    
  return filtered_dependencies

#---------------------create shadow library with filtered dependencies-----------------------------
def create_shadow_directory(filtered_deps, target_dir):
  """
    Create a shadow directory and populates it with 
    symbolic links pointing to /dev/null.
  """
  shadow_dir = os.path.abspath(os.path.join(target_dir, "tmp"))
  os.makedirs(shadow_dir, exist_ok=True)
  print(f"Creating shadow dir at : {shadow_dir}")

  # defining a fake not null library
  shadow_lib = os.path.join(shadow_dir, "libdummy.so")

  # creating fake shared lib if it doesn't exist
  if not os.path.exists(shadow_lib):
    try:
      subprocess.run(
          ["gcc", "-shared", "-o", shadow_lib, "-fPIC", "-xc", "-"],
          input='int dummy() { return 0; }',
          text=True, 
          check=True
         )
      print(f"Created shadow library: {shadow_lib}");
    except subprocess.CalledProcessError as e:
      print(f"Error creating fake library: {e}")
      return None

  
  for sym_name in filtered_deps:
    link_path = os.path.join(shadow_dir, sym_name)
    
    # check if it exisits already
    if os.path.exists(link_path) or os.path.islink(link_path):
      print(f"Symlink already exists: {link_path}, skipping...")
    else:
      try:
        os.symlink(shadow_lib, link_path) # point sym link to /dev/null
        print(f"Created symlink: {link_path} -> {shadow_lib}")
      except OSError as e:
        # due to race condition, failed msg is seen for clready created links
        #print(f"Failed to create symlink {link_path}: {e}")
        pass

  return shadow_dir

#-------------------------------main---------------------------------------
if __name__=="__main__":
  if len(sys.argv) != 3:
    print(f"USAGE: mana_shadow_mpi_libs.py  <executable_path> <shadow_dir_path>")
    sys.exit(1)

  executable = sys.argv[1]        # Executable used for creating shadow lib of
  shadow_lib_path = sys.argv[2]   # Location to create a shadow lib

  # check if the executable is valid and can be executed 
  if not os.path.isfile(executable) or not os.access(executable, os.X_OK):
    print(f"ERROR: {executable} is not a valid executable file")
    sys.exit(1)

  filtered_deps = filter_library_dependencies(executable)

  if filtered_deps:
    shadow_dir = create_shadow_directory(filtered_deps, shadow_lib_path)
    print(f"Shadow directory with filtered symlinks created: {shadow_dir}")
  else:
    print("No matching libraries found.")

