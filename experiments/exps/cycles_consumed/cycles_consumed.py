import experiments as exp

from exps.cycles_consumed.configs.bare_vtas import Config as VTasBareConf

experiments = []

exp_name = "cycles-consumed_"
vtas_bare_exp = exp.Experiment(VTasBareConf(exp_name + "bare-vtas"), name=exp_name)

experiments.append(vtas_bare_exp)
  
