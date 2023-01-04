import experiments as exp
from configs.eval.scalability.linux_bare import Config as LinBareConf
from configs.eval.scalability.linux_virt import Config as LinVirtConf
from configs.eval.scalability.tas_bare import Config as TasBareConf
from configs.eval.scalability.tas_virt import Config as TasVirtConf
  
experiments = []
# lin_bare_exp = exp.Experiment(LinBareConf(), name="linux")
# lin_virt_exp = exp.Experiment(LinVirtConf(), name="linux")
# tas_bare_exp = exp.Experiment(TasBareConf(), name="tas")
tas_virt_exp = exp.Experiment(TasVirtConf(), name="tas")

# experiments.append(lin_bare_exp)
# experiments.append(lin_virt_exp)
# experiments.append(tas_bare_exp)
experiments.append(tas_virt_exp)
