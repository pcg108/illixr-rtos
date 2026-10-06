package firechip.chip

import org.chipsalliance.cde.config.Config

/** FireSim adaptation of the existing 500 MHz Rocket/Zephyr validation platform.
  *
  * These are target frequencies. The U250 simulator's FPGA host frequency is a
  * separate build-recipe setting and must not change the Zephyr CLINT timebase.
  * Retain the stock Rocket UART, serial TL width, inclusive cache, and boot ROM;
  * the firmware uses HTIF through the TSI bridge, with FASED-backed ELF loading.
  */
class WithILLIXRFireSimPlatform extends Config(
  new freechips.rocketchip.subsystem.WithExtMemSize(1L << 28) ++
  new freechips.rocketchip.subsystem.WithTimebase(BigInt(500000)) ++
  new chipyard.config.WithSystemBusFrequency(500.0) ++
  new chipyard.config.WithControlBusFrequency(500.0) ++
  new chipyard.config.WithPeripheryBusFrequency(500.0) ++
  new chipyard.config.WithMemoryBusFrequency(500.0) ++
  new chipyard.config.WithFrontBusFrequency(500.0) ++
  new chipyard.config.WithOffchipBusFrequency(500.0) ++
  new chipyard.harness.WithHarnessBinderClockFreqMHz(500.0) ++
  new WithDefaultFireSimBridgesWithoutTracerV ++
  new WithMinimalFireSimDesignTweaks
)

class FireSimILLIXRSingleRocketConfig extends Config(
  new chipyard.config.WithNPerfCounters(13) ++
  new WithILLIXRFireSimPlatform ++
  new chipyard.RocketConfig
)

class FireSimILLIXRDualRocketConfig extends Config(
  new chipyard.config.WithNPerfCounters(13) ++
  new WithILLIXRFireSimPlatform ++
  new chipyard.DualRocketConfig
)

class FireSimILLIXRQuadRocketConfig extends Config(
  new chipyard.config.WithNPerfCounters(13) ++
  new WithILLIXRFireSimPlatform ++
  new chipyard.QuadRocketConfig
)

class FireSimILLIXRSingleShuttleSaturnConfig extends Config(
  new WithILLIXRFireSimPlatform ++
  new chipyard.REFV256D128ShuttleConfig
)
class FireSimILLIXRQuadShuttleSaturnConfig extends Config(
  new WithILLIXRFireSimPlatform ++
  new saturn.shuttle.WithShuttleVectorUnit(256, 128, saturn.common.VectorParams.refParams) ++
  new chipyard.config.WithSystemBusWidth(128) ++
  new shuttle.common.WithShuttleTileBeatBytes(16) ++
  new shuttle.common.WithNShuttleCores(4) ++
  new chipyard.config.AbstractConfig
)
