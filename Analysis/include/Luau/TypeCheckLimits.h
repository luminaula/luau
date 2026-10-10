// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Cancellation.h"
#include "Luau/Error.h"
#include "Luau/TimeTrace.h"

#include <memory>
#include <optional>
#include <string>

namespace Luau
{

class TimeLimitError : public InternalCompilerError
{
public:
    explicit TimeLimitError(const std::string& moduleName)
        : InternalCompilerError("Typeinfer failed to complete in allotted time", moduleName)
    {
    }
};

class UserCancelError : public InternalCompilerError
{
public:
    explicit UserCancelError(const std::string& moduleName)
        : InternalCompilerError("Analysis has been cancelled by user", moduleName)
    {
    }
};

struct TypeCheckLimits
{
    std::optional<double> finishTime;
    std::optional<int> instantiationChildLimit;
    std::optional<int> unifierIterationLimit;

    std::shared_ptr<FrontendCancellationToken> cancellationToken;
};

// Throw TimeLimitError naming `moduleName` once the finish time of `limits` has passed, and
// UserCancelError once its cancellation token has been requested. The passes whose work grows
// with the types they meet call it: the checking passes after constraint solving, subtyping and
// type-function reduction, so the module's time limit and a cancellation reach them as they
// reach constraint solving.
inline void checkTypeCheckLimits(const TypeCheckLimits& limits, const std::string& moduleName)
{
    if (limits.finishTime && TimeTrace::getClock() > *limits.finishTime)
        throw TimeLimitError(moduleName);

    if (limits.cancellationToken && limits.cancellationToken->requested())
        throw UserCancelError(moduleName);
}

} // namespace Luau
