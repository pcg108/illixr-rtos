package firechip.chip

import org.chipsalliance.cde.config.Config

class FireSimILLIXRSingleRocketDualGemminiSaturnConfig extends Config(
  new chipyard.config.WithNPerfCounters(13) ++
  new WithILLIXRFireSimPlatform ++
  new chipyard.ILLIXRSingleRocketDualGemminiSaturnConfig)

class FireSimILLIXRSingleRocketInt8GemminiSaturnConfig extends Config(
  new chipyard.config.WithNPerfCounters(13) ++
  new WithILLIXRFireSimPlatform ++
  new chipyard.ILLIXRSingleRocketInt8GemminiSaturnConfig)

class FireSimILLIXRQuadRocketDualGemminiSaturnConfig extends Config(
  new chipyard.config.WithNPerfCounters(13) ++
  new WithILLIXRFireSimPlatform ++
  new chipyard.ILLIXRQuadRocketDualGemminiSaturnConfig)

class FireSimILLIXRQuadRocketInt8GemminiSaturnConfig extends Config(
  new chipyard.config.WithNPerfCounters(13) ++
  new WithILLIXRFireSimPlatform ++
  new chipyard.ILLIXRQuadRocketInt8GemminiSaturnConfig)
