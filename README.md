This repository is forked from [gStore](https://github.com/pkumod/gStore) and implements the SPARQL optimizations described in the paper "gFOV: A Full-Stack SPARQL Query Optimizer & Plan Visualizer."

The optimizations are implemented in the function `GeneralEvaluation::highLevelOpt` (Query/GeneralEvaluation.cpp).

## Installation and Usage

To compile, first run `make pre`, then run `make`. If compilation is unsuccessful due to a lack of system dependencies, please run the setup script under the directory `scripts/setup` corresponding to your environment.

To run a SPARQL query on a dataset, please first build the dataset by running:

```bash
$ bin/gbuild <database_name> <dataset_path>
```

In our demonstration, we use the dataset `data/lubm/lubm.nt`.
