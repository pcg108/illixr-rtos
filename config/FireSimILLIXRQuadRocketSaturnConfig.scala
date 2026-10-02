package firechip.chip

import org.chipsalliance.cde.config.Config

/** Four-core counterpart of FireSimILLIXRSingleRocketSaturnConfig.
  * Keep all tile, vector, cache, bus and platform parameters identical;
  * only the number of Rocket/Saturn tiles changes.
  */
class FireSimILLIXRQuadRocketSaturnConfig extends Config(
  new WithILLIXRFireSimPlatform ++
  new saturn.rocket.WithRocketVectorUnit(
    256, 128, saturn.common.VectorParams.refParams,
    useL1DCache = false, mLen = Some(128)) ++
  new freechips.rocketchip.rocket.WithL1DCacheWays(4) ++
  new freechips.rocketchip.rocket.WithL1DCacheNonblocking(4) ++
  new chipyard.config.WithSystemBusWidth(128) ++
  new freechips.rocketchip.rocket.WithNHugeCores(4) ++
  new chipyard.config.AbstractConfig
)
