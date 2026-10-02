package firechip.chip

import org.chipsalliance.cde.config.Config

class FireSimILLIXRSingleRocketDualGemminiSaturnConfig extends Config(
  new WithILLIXRFireSimPlatform ++
  new chipyard.ILLIXRSingleRocketDualGemminiSaturnConfig)

class FireSimILLIXRSingleRocketInt8GemminiSaturnConfig extends Config(
  new WithILLIXRFireSimPlatform ++
  new chipyard.ILLIXRSingleRocketInt8GemminiSaturnConfig)

class FireSimILLIXRQuadRocketDualGemminiSaturnConfig extends Config(
  new WithILLIXRFireSimPlatform ++
  new chipyard.ILLIXRQuadRocketDualGemminiSaturnConfig)

class FireSimILLIXRQuadRocketInt8GemminiSaturnConfig extends Config(
  new WithILLIXRFireSimPlatform ++
  new chipyard.ILLIXRQuadRocketInt8GemminiSaturnConfig)
