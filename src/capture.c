#include <Rinternals.h>

#define attribute_hidden
#define _(string) (string)


SEXP attribute_hidden capture_arg(SEXP x, SEXP env) {
    static SEXP nms = NULL;
    if (!nms) {
        nms = allocVector(STRSXP, 2);
        R_PreserveObject(nms);
        SET_STRING_ELT(nms, 0, mkChar("expr"));
        SET_STRING_ELT(nms, 1, mkChar("env"));
    }

    SEXP info = PROTECT(allocVector(VECSXP, 2));
    SET_VECTOR_ELT(info, 0, x);
    SET_VECTOR_ELT(info, 1, env);
    setAttrib(info, R_NamesSymbol, nms);

    UNPROTECT(1);
    return info;
}

SEXP attribute_hidden capture_promise(SEXP x, int strict) {
    // If promise was optimised away, return the literal
    if (TYPEOF(x) != PROMSXP)
        return capture_arg(x, R_EmptyEnv);

    SEXP env = R_NilValue;
    while (TYPEOF(x) == PROMSXP) {
        env = PRENV(x);
        x = PREXPR(x);
    }
    if (env == R_NilValue) {
        if (strict)
            error(_("the argument has already been evaluated"));
        else
            return R_NilValue;
    }

    MARK_NOT_MUTABLE(x);
    return capture_arg(x, env);
}

SEXP attribute_hidden rlang_capturearg(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    int strict = asLogical(CADR(args));
    SEXP arg = findVarInFrame3(rho, install("x"), TRUE);

    // Happens when argument is unwrapped from a promise by the compiler
    if (TYPEOF(arg) != PROMSXP)
        return capture_arg(arg, R_EmptyEnv);

    // Get promise in caller frame
    SEXP caller_env = CAR(args);
    SEXP sym = PREXPR(arg);
    if (TYPEOF(sym) != SYMSXP)
        error(_("\"x\" must be an argument name"));

    arg = findVarInFrame3(caller_env, sym, TRUE);
    if (arg == R_UnboundValue)
        error(_("Attempt to capture argument that is not part of function signature"));
    else
        return capture_promise(arg, strict);
}

SEXP attribute_hidden rlang_capturedots(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    SEXP caller_env = CAR(args);
    int strict = asLogical(CADR(args));

    // R code has checked for unbound dots
    SEXP dots = PROTECT(findVarInFrame3(caller_env, R_DotsSymbol, TRUE));

    if (dots == R_MissingArg) {
        UNPROTECT(1);
        return allocVector(VECSXP, 0);
    }

    int n_dots = length(dots);
    SEXP captured = PROTECT(allocVector(VECSXP, n_dots));

    SEXP names = PROTECT(allocVector(STRSXP, n_dots));
    Rboolean named = FALSE;

    SEXP dot;
    int i = 0;
    while (i != n_dots) {
        dot = CAR(dots);

        if (TYPEOF(dot) == PROMSXP) {
            dot = capture_promise(dot, strict);
            if (dot == R_NilValue) {
                UNPROTECT(3);
                return R_NilValue;
            }
        } else {
            dot = capture_arg(dot, R_EmptyEnv);
        }
        SET_VECTOR_ELT(captured, i, dot);

        if (TAG(dots) != R_NilValue) {
            named = TRUE;
            SET_STRING_ELT(names, i, PRINTNAME(TAG(dots)));
        }

        ++i;
        dots = CDR(dots);
    }

    if (named)
        setAttrib(captured, R_NamesSymbol, names);

    UNPROTECT(3);
    return captured;
}
