import experiments as exp
from configs.eval.scalability.linux_bare import Config as LinBareConf
from configs.eval.scalability.linux_virt import Config as LinVirtConf
from configs.eval.scalability.tas_bare import Config as TasBareConf
from configs.eval.scalability.tas_virt import Config as TasVirtConf


num_cons = [1, 5, 10, 15, 20, 25]
experiments = []
for i in num_cons:
  # lin_bare_exp = exp.Experiment(LinBareConf(), name="linux")
  lin_virt_exp = exp.Experiment(LinVirtConf(i), name="linux_conn_{}".format(i))
  # tas_bare_exp = exp.Experiment(TasBareConf(), name="tas")
  tas_virt_exp = exp.Experiment(TasVirtConf(i), name="tas_conn_{}".format(i))

  # experiments.append(lin_bare_exp)
  experiments.append(lin_virt_exp)
  # experiments.append(tas_bare_exp)
  experiments.append(tas_virt_exp)
  
