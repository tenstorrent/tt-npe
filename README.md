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
As of now, the only way to run the code is via the executable `tt_npe_run`

```shell
source ENV_SETUP # setup $PATH to access tt_npe_run
tt_npe_run <yaml_config>
```

###### Example 1
```shell
tt_npe_run config/1d