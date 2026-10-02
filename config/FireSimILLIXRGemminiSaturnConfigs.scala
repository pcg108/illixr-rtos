package firechip.chip

import org.chipsalliance.cde.config.Config

class FireSimILLIXRSingleRocketGemminiSaturnConfig extends Config(
  new WithILLIXRFireSimPlatform ++
  new chipyard.ILLIXRSingleRocketGemminiSaturnConfig)

class FireSimILLIXRQuadRocketGemminiSaturnConfig extends Config(
  new WithILLIXRFireSimPlatform ++
  new chipyard.ILLIXRQuadRocketGemminiSaturnConfig)
