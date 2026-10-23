"""Independent directed-decimal enclosures; no binary host libm calls.

exp uses positive Taylor series at |x|/2**k <=1/16, explicit geometric
remainder, directed rounding and repeated squaring. tanh uses exp(2|x|).
sqrt candidates are certified by exact rational squaring. IEEE conversion is
imported from accepted M8's integer/Fraction nearest-even oracle.
"""
from __future__ import annotations
from decimal import Context, Decimal, ROUND_FLOOR, ROUND_CEILING
from fractions import Fraction
from functools import lru_cache
from exact_numeric_m8 import finite_value, round_bits, parts

PRECISION = 240
TERMS = 100


def contexts(precision: int) -> tuple[Context, Context]:
    return (Context(prec=precision, rounding=ROUND_FLOOR, Emin=-999999, Emax=999999),
            Context(prec=precision, rounding=ROUND_CEILING, Emin=-999999, Emax=999999))


def decimal_bounds(value: Fraction, low: Context, high: Context) -> tuple[Decimal, Decimal]:
    return (low.divide(Decimal(value.numerator), Decimal(value.denominator)),
            high.divide(Decimal(value.numerator), Decimal(value.denominator)))


def exp_interval(value: Fraction, precision: int = PRECISION) -> tuple[Decimal, Decimal]:
    """Enclose e**value, using only directed decimal rational operations."""
    low, high = contexts(precision)
    negative = value < 0
    value = abs(value)
    reductions = 0
    while value > Fraction(1,16):
        value /= 2
        reductions += 1
    rlo, rhi = decimal_bounds(value, low, high)
    tlo = thi = slo = shi = Decimal(1)
    for n in range(1, TERMS + 1):
        tlo = low.divide(low.multiply(tlo,rlo), Decimal(n))
        thi = high.divide(high.multiply(thi,rhi), Decimal(n))
        slo, shi = low.add(slo,tlo), high.add(shi,thi)
    # For t_(N+1+j), successive ratio <= r/(N+2); the entire omitted
    # positive tail <= t_(N+1)/(1-r/(N+2)). Bounds are directed outward.
    first_tail = high.divide(high.multiply(thi,rhi), Decimal(TERMS+1))
    ratio = high.divide(rhi, Decimal(TERMS+2))
    tail = high.divide(first_tail, low.subtract(Decimal(1), ratio))
    shi = high.add(shi,tail)
    for _ in range(reductions):
        slo, shi = low.multiply(slo,slo), high.multiply(shi,shi)
    if negative:
        return low.divide(Decimal(1),shi), high.divide(Decimal(1),slo)
    return slo, shi


def sqrt_interval(value: Fraction, precision: int = PRECISION) -> tuple[Decimal, Decimal]:
    if value < 0:
        raise ValueError('negative square root')
    if not value:
        return Decimal(0), Decimal(0)
    low, high = contexts(precision)
    # Context.sqrt is independently implemented by libmpdec and correctly
    # rounded. Expand one decimal representable each way, then certify the
    # enclosure using exact Fraction squares, independent of that promise.
    xlo, xhi = decimal_bounds(value,low,high)
    lo = low.next_minus(low.sqrt(xlo))
    hi = high.next_plus(high.sqrt(xhi))
    if not (Fraction(lo)*Fraction(lo) <= value <= Fraction(hi)*Fraction(hi)):
        raise ArithmeticError('sqrt enclosure rational certificate failed')
    return lo, hi


def ideal_interval(operation: str, value: Fraction, precision: int = PRECISION) -> tuple[Decimal, Decimal]:
    if operation == 'exp':
        return exp_interval(value,precision)
    if operation == 'sqrt':
        return sqrt_interval(value,precision)
    if operation != 'tanh':
        raise ValueError('unknown independent operation')
    if not value:
        return Decimal(0), Decimal(0)
    low, high = contexts(precision)
    elo,ehi = exp_interval(2*abs(value),precision)
    lo = low.subtract(Decimal(1), high.divide(Decimal(2),low.add(elo,Decimal(1))))
    hi = high.subtract(Decimal(1), low.divide(Decimal(2),high.add(ehi,Decimal(1))))
    return (hi.copy_negate(),lo.copy_negate()) if value < 0 else (lo,hi)


def ordered(bits: int, width: int) -> int:
    sign = 1 << (width-1)
    return ((1 << width)-1-bits) if bits & sign else sign+bits


def from_ordered(value: int, width: int) -> int:
    sign = 1 << (width-1)
    return ((1 << width)-1-value) if value < sign else value-sign


@lru_cache(maxsize=None)
def reference(operation: str, input_bits: int, width: int = 32) -> dict:
    value = finite_value(input_bits,width)
    lo,hi = ideal_interval(operation,value)
    negzero = bool(input_bits >> (width-1)) and not value and operation != 'exp'
    a,b = round_bits(Fraction(lo),width,negzero),round_bits(Fraction(hi),width,negzero)
    if a != b:
        raise ArithmeticError('enclosure does not establish unique nearest-even result')
    mantissa,exponent_bits,bias = parts(width)
    if ((a >> mantissa) & ((1 << exponent_bits)-1)) == (1 << exponent_bits)-1:
        return dict(operation=operation,width=width,input_bits=input_bits,lo=str(lo),hi=str(hi),nearest_bits=a,allowed_bits=[])
    if operation == 'sqrt' or not value:
        allowed = [a]
    else:
        midpoint = abs((Fraction(lo)+Fraction(hi))/2)
        exponent = midpoint.numerator.bit_length()-midpoint.denominator.bit_length()
        if (midpoint < (Fraction(1<<exponent) if exponent >= 0 else Fraction(1,1<<-exponent))):
            exponent -= 1
        shift = max(exponent,1-bias)-mantissa
        ulp = Fraction(1<<shift) if shift >= 0 else Fraction(1,1<<-shift)
        allowed=[]
        for index in range(ordered(a,width)-3,ordered(a,width)+4):
            bits=from_ordered(index,width)
            if not 0 <= bits < 1<<width:
                continue
            try:result=finite_value(bits,width)
            except ValueError:continue
            if max(abs(result-Fraction(lo)),abs(result-Fraction(hi))) <= 2*ulp:
                allowed.append(bits)
        if a not in allowed:
            raise ArithmeticError('nearest representable is outside accuracy policy')
    return dict(operation=operation,width=width,input_bits=input_bits,lo=str(lo),hi=str(hi),nearest_bits=a,allowed_bits=allowed)


def controls() -> dict:
    checks=0
    # Hardcoded independently published mathematical digits act only as loose
    # rational outer bounds; the enclosure computation does not consume them.
    lo,hi=exp_interval(Fraction(1))
    assert Decimal('2.71828182845904523536028747135266249775724709369995') < lo <= hi < Decimal('2.71828182845904523536028747135266249775724709369997')
    checks+=1
    for value in [Fraction(0),Fraction(1,1024),Fraction(1,2),Fraction(3),Fraction(80),Fraction(709),Fraction(-745)]:
        lo,hi=exp_interval(value)
        assert lo<=hi and (hi-lo) / hi < Decimal('1e-180')
        l2,h2=exp_interval(-value)
        assert Fraction(lo)*Fraction(l2) <= 1 <= Fraction(hi)*Fraction(h2)
        low180,high180=exp_interval(value,180)
        assert low180<=lo<=hi<=high180
        checks+=3
    for value in [Fraction(0),Fraction(1),Fraction(2),Fraction(4),Fraction(1,1<<149),Fraction(1<<127)]:
        lo,hi=sqrt_interval(value)
        assert Fraction(lo)**2 <= value <= Fraction(hi)**2
        checks+=1
    for operation in ['exp','tanh','sqrt']:
        for width in [32,64]:
            for value in [Fraction(0),Fraction(1,4),Fraction(1),Fraction(2),Fraction(4)]:
                item=reference(operation,round_bits(value,width),width)
                assert item['nearest_bits'] in item['allowed_bits']
                checks+=1
    return dict(passed=True,checks=checks,precision=PRECISION,taylor_terms=TERMS,host_libm_used=False)


if __name__=='__main__':
    import json
    print(json.dumps(controls(),indent=2))
