import experiments as exp

from exps.scalability.configs.tap_tas import Config as TapTasConf
from exps.scalability.configs.virt_linux import Config as VirtLinuxConf
from exps.scalability.configs.bare_linux import Config as BareLinuxConf
from exps.scalability.configs.bare_tas import Config as TasBareConf
from exps.scalability.configs.bare_vtas import Config as VTasBareConf
from exps.scalability.configs.virt_tas import Config as TasVirtConf

num_cons = [100, 200, 300, 400, 500, 600, 700, 800]
experiments = []
for n in num_cons:
  exp_name = "scalability"
  tas_tap_exp = exp.Experiment(TapTasConf(exp_name, n), name=exp_name)
  lin_bare_exp = exp.Experiment(BareLinuxConf(exp_name, n), name=exp_name)
  lin_virt_exp = exp.Experiment(VirtLinuxConf(exp_name, n), name=exp_name)
  tas_bare_exp = exp.Experiment(TasBareConf(exp_name, n), name=exp_name)
  vtas_bare_exp = exp.Experiment(VTasBareConf(exp_name, n), name=exp_name)
  tas_virt_exp = exp.Experiment(TasVirtConf(exp_name, n), name=exp_name)

  experiments.append(tas_tap_exp)
  experiments.append(lin_bare_exp)
  experiments.append(lin_virt_exp)
  experiments.append(tas_bare_exp)
  experiments.append(vtas_bare_exp)
  experiments.append(tas_virt_exp)
  
