# Transcendentals Require Ring Extensions

This directory documents the theoretical foundation for the optimizations found in `FMT_Ring.h` and the fused 3D pipeline.

## Theory

Functions like:
- $\log_2(x)$
- $\exp_2(x)$
- $\sin(x)$
- Perspective scale

are not simple ring operations in the base numeric ring $R$ (e.g., integers or fixed-point numbers). They correspond to transcendental elements such as $\log(2)$, $e$, and $\pi$, which do not live inside $R$.

The lookup table generator constructs a finite algebraic extension:
$$R \subset R[T]/(P(T))$$
where the lookup table (LUT) itself acts as the defining relation for the extension. In this view, tables are algebraic extensions of the numeric ring.

## Application in FMT

By treating the log domain as an extension, we can perform multiplications and divisions as simple additions and subtractions within the "extended" ring.

### Domain Persistence (Ring Extension Optimization)

Instead of converting back and forth between linear and log domains for every operation, we keep values in the log domain (using the `Log32` type) as long as possible. This is particularly effective for:
- **Fused Multiplications**: $a \cdot b \cdot c$ becomes $exp(log(a) + log(b) + log(c))$, saving intermediate $exp/log$ calls.
- **Perspective Projection**: Fusing scaling and perspective division into a single log-domain subtraction.

## Performance Results (AVR)

- **Standard MVP Pipeline**: ~10390 cycles
- **Fused MVP Pipeline**: ~9286 cycles (~10% improvement)

By maintaining the "extension" state, we reduce the computational overhead of transcendental approximations.
