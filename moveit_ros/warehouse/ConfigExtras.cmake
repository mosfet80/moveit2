# Extras module needed for dependencies to find boost components

find_package(
  Boost
  REQUIRED
  thread
  system
  filesystem
  random
  regex
  date_time
  program_options)
