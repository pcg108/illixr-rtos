package firechip.chip

import org.chipsalliance.cde.config.Config

/** Rocket comparison for the ILLIXR REFV256D128 Shuttle/Saturn platform.
  * Keep the vector backend, external coherent memory path, cache capacities,
  * outstanding D-cache miss count, and system-bus width aligned with Shuttle.
  * The scalar pipelines and cache implementations remain core-specific.
  * WithILLIXRFireSimPlatform supplies the common clocks, bridges and 256 MiB RAM.
  */
class FireSimILLIXRSingleRocketSaturnConfig extends Config(
  new chipyard.config.WithNPerfCounters(13) ++
  new WithILLIXRFireSimPlatform ++
  new saturn.rocket.WithRocketVectorUnit(
    256, 128, saturn.common.VectorParams.refParams,
    useL1DCache = false, mLen = Some(128)) ++
  new freechips.rocketchip.rocket.WithL1DCacheWays(4) ++
  new freechips.rocketchip.rocket.WithL1DCacheNonblocking(4) ++
  new chipyard.config.WithSystemBusWidth(128) ++
  new freechips.rocketchip.rocket.WithNHugeCores(1) ++
  new chipyard.config.AbstractConfig
)
