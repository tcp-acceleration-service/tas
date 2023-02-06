import experiments as exp
from configs.eval.scalability.linux_bare import Config as LinBareConf
from configs.eval.scalability.linux_virt import Config as LinVirtConf
from configs.eval.scalability.tas_bare import Config as TasBareConf
from configs.eval.scalability.tas_virt import Config as TasVirtConf


num_cons = [10]
experiments = []
for n in num_cons:
  # lin_bare_exp = exp.Experiment(LinBareConf(n), name="linux_conn_{}".format(n))
  # lin_virt_exp = exp.Experiment(LinVirtConf(n), name="linux_conn_{}".format(n))
  # tas_bare_exp = exp.Experiment(TasBareConf(n), name="tas_conn_{}".format(n))
  tas_virt_exp = exp.Experiment(TasVirtConf(n), name="tas_conn_{}".format(n))

  # experiments.append(lin_bare_exp)
  # experiments.append(lin_virt_exp)
  # experiments.append(tas_bare_exp)
  experiments.append(tas_virt_exp)
  
