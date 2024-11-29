## tt-npe — Lightweight Network-on-Chip Performance Estimator

### Build

_Note: For now only pre-approved collaborators can pull this repo._ Please ask **bgrady-tt** for access!

```shell
git clone git@github.com:bgrady-tt/tt-npe.git

cd tt-npe/
source ENV_SETUP      # sets environment variables
./build-noc-model.sh  # runs cmake internally
```

### Usage

##### C++ CLI
As of now, the only way to run the code is via the executable `tt_npe_run`. 

> [!NOTE]
> Nov 25 2024: Don't expect much here; basic functionality is still being developed and interim hacks won't be documented.

```shell
cd tt-npe/ 
source ENV_SETUP # setup $PATH to access tt_npe_run
tt_npe_run --help
```

**Examples**
```shell
# runs a pre-defined workflow from a yaml file
tt_npe_run --workload workload/example.yaml

# runs a randomly generated NoC workload fuzz test
tt_npe_run --test-config config/random.yaml
```
