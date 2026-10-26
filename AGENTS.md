# Contributing with coding agents

Read the README and affected API documentation first. Preserve unrelated work.
Use feature branches and pull requests against main. GitHub issues track work.

Keep native behavior shared by all language bindings. Preserve explicit units,
coordinate frames, resource validity, cancellation and handle ownership.
Distinguish synthetic tests, consistency, conditional precision and physical accuracy.
Never invent missing measurements or uncertainties.

Only independent synthetic fixtures belong in this repository. Do not add recordings,
location-bearing observations, credentials, personal paths or private logs. Keep replay
inputs and outputs outside Git. Run tools/check_publication.py before committing.
Document public contract changes and run the relevant regression suites.
Use 12 workers when configuring build or test parallelism. Do not access cameras.
