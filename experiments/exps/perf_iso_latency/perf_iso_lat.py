import experiments as exp

from exps.perf_iso_latency.configs.bare_tas import Config as TasBareConf
from exps.perf_iso_latency.configs.bare_vtas import Config as VTasBareConf
from exps.perf_iso_latency.configs.virt_tas import Config as TasVirtConf

experiments = []

exp_name = "perf_iso_latency-"
# tas_bare_exp = exp.Experiment(TasBareConf(exp_name), name=exp_name)
vtas_bare_exp = exp.Experiment(VTasBareConf(exp_name + "bare-vtas"), name=exp_name)
# tas_virt_exp = exp.Experiment(TasVirtConf(exp_name), name=exp_name)

# experiments.append(tas_bare_exp)
experiments.append(vtas_bare_exp)
# experiments.append(tas_virt_exp)
  
