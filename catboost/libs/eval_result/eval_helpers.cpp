#include "eval_helpers.h"

#include <catboost/libs/helpers/exception.h>
#include <catboost/libs/labels/label_helper_builder.h>

#include <library/fast_exp/fast_exp.h>

#include <util/generic/algorithm.h>
#include <util/generic/utility.h>
#include <util/string/cast.h>
#include <util/generic/array_ref.h>

#include <cmath>
#include <limits>

void CalcSoftmax(const TConstArrayRef<double> approx, TVector<double>* softmax) {
    double maxApprox = *MaxElement(approx.begin(), approx.end());
    for (size_t dim = 0; dim < approx.size(); ++dim) {
        (*softmax)[dim] = approx[dim] - maxApprox;
    }
    FastExpInplace(softmax->data(), softmax->ysize());
    double sumExpApprox = 0;
    for (auto curSoftmax : *softmax) {
        sumExpApprox += curSoftmax;
    }
    for (auto& curSoftmax : *softmax) {
        curSoftmax /= sumExpApprox;
    }
}

void CalcLogSoftmax(const TConstArrayRef<double> approx, TVector<double>* softmax) {
    double maxApprox = *MaxElement(approx.begin(), approx.end());
    for (size_t dim = 0; dim < approx.size(); ++dim) {
        (*softmax)[dim] = approx[dim] - maxApprox;
    }
    FastExpInplace(softmax->data(), softmax->ysize());
    double sumExpApprox = 0;
    for (auto curSoftmax : *softmax) {
        sumExpApprox += curSoftmax;
    }
    const double logSumExpApprox = log(sumExpApprox);
    for (size_t dim = 0; dim < approx.size(); ++dim) {
        (*softmax)[dim] = approx[dim] - maxApprox - logSumExpApprox;
    }
}

TVector<double> CalcSigmoid(const TConstArrayRef<double> approx) {
    TVector<double> probabilities;
    probabilities.yresize(approx.size());
    for (size_t i = 0; i < approx.size(); ++i) {
        probabilities[i] = 1. / (1. + exp(-approx[i]));
    }
    return probabilities;
}

TVector<double> CalcNegativeSigmoid(TConstArrayRef<double> approx) {
    TVector<double> probabilities;
    probabilities.yresize(approx.size());
    for (size_t i = 0; i < approx.size(); ++i) {
        probabilities[i] = 1. / (1. + exp(approx[i]));
    }
    return probabilities;
}

TVector<double> CalcLogSigmoid(const TConstArrayRef<double> approx) {
    TVector<double> probabilities;
    probabilities.yresize(approx.size());
    for (size_t i = 0; i < approx.size(); ++i) {
        probabilities[i] = -log(1. + exp(-approx[i]));
    }
    return probabilities;
}

TVector<double> CalcNegativeLogSigmoid(const TConstArrayRef<double> approx) {
    TVector<double> probabilities;
    probabilities.yresize(approx.size());
    for (size_t i = 0; i < approx.size(); ++i) {
        probabilities[i] = -log(1. + exp(approx[i]));
    }
    return probabilities;
}

static TVector<TVector<double>> CalcSoftmax(
    const TVector<TVector<double>>& approx,
    NPar::TLocalExecutor* executor)
{
    TVector<TVector<double>> probabilities = approx;
    probabilities.resize(approx.size());
    ForEach(probabilities.begin(), probabilities.end(), [&](auto& v) { v.yresize(approx.front().size()); });
    const int executorThreadCount = executor ? executor->GetThreadCount() : 0;
    const int threadCount = executorThreadCount + 1;
    const int blockSize = (approx[0].ysize() + threadCount - 1) / threadCount;
    const auto calcSoftmaxInBlock = [&](const int blockId) {
        int lastLineId = Min((blockId + 1) * blockSize, approx[0].ysize());
        TVector<double> line;
        line.yresize(approx.size());
        TVector<double> softmax;
        softmax.yresize(approx.size());
        for (int lineInd = blockId * blockSize; lineInd < lastLineId; ++lineInd) {
            for (int dim = 0; dim < approx.ysize(); ++dim) {
                line[dim] = approx[dim][lineInd];
            }
            CalcSoftmax(line, &softmax);
            for (int dim = 0; dim < approx.ysize(); ++dim) {
                probabilities[dim][lineInd] = softmax[dim];
            }
        }
    };
    if (executor) {
        executor->ExecRange(calcSoftmaxInBlock, 0, threadCount, NPar::TLocalExecutor::WAIT_COMPLETE);
    } else {
        calcSoftmaxInBlock(0);
    }
    return probabilities;
}

static TVector<TVector<double>> CalcLogSoftmax(
    const TVector<TVector<double>>& approx,
    NPar::TLocalExecutor* executor)
{
    TVector<TVector<double>> probabilities = approx;
    probabilities.resize(approx.size());
    ForEach(probabilities.begin(), probabilities.end(), [&](auto& v) { v.yresize(approx.front().size()); });
    const int executorThreadCount = executor ? executor->GetThreadCount() : 0;
    const int threadCount = executorThreadCount + 1;
    const int blockSize = (approx[0].ysize() + threadCount - 1) / threadCount;
    const auto calcSoftmaxInBlock = [&](const int blockId) {
        int lastLineId = Min((blockId + 1) * blockSize, approx[0].ysize());
        TVector<double> line;
        line.yresize(approx.size());
        TVector<double> softmax;
        softmax.yresize(approx.size());
        for (int lineInd = blockId * blockSize; lineInd < lastLineId; ++lineInd) {
            for (int dim = 0; dim < approx.ysize(); ++dim) {
                line[dim] = approx[dim][lineInd];
            }
            CalcLogSoftmax(line, &softmax);
            for (int dim = 0; dim < approx.ysize(); ++dim) {
                probabilities[dim][lineInd] = softmax[dim];
            }
        }
    };
    if (executor) {
        executor->ExecRange(calcSoftmaxInBlock, 0, threadCount, NPar::TLocalExecutor::WAIT_COMPLETE);
    } else {
        calcSoftmaxInBlock(0);
    }
    return probabilities;
}

static TVector<int> SelectBestClass(
    const TVector<TVector<double>>& approx,
    NPar::TLocalExecutor* executor)
{
    TVector<int> classApprox;
    classApprox.yresize(approx.front().size());
    const int executorThreadCount = executor ? executor->GetThreadCount() : 0;
    const int threadCount = executorThreadCount + 1;
    const int blockSize = (approx[0].ysize() + threadCount - 1) / threadCount;
    const auto selectBestClassInBlock = [&](const int blockId) {
        int lastLineId = Min((blockId + 1) * blockSize, approx[0].ysize());
        for (int lineInd = blockId * blockSize; lineInd < lastLineId; ++lineInd) {
            double maxApprox = approx[0][lineInd];
            int maxApproxId = 0;
            for (int dim = 1; dim < approx.ysize(); ++dim) {
                if (approx[dim][lineInd] > maxApprox) {
                    maxApprox = approx[dim][lineInd];
                    maxApproxId = dim;
                }
            }
            classApprox[lineInd] = maxApproxId;
        }
    };
    if (executor) {
        executor->ExecRange(selectBestClassInBlock, 0, threadCount, NPar::TLocalExecutor::WAIT_COMPLETE);
    } else {
        selectBestClassInBlock(0);
    }
    return classApprox;
}

bool IsMulticlass(const TVector<TVector<double>>& approx) {
    return approx.size() > 1;
}

void MakeExternalApprox(
    const TVector<TVector<double>>& internalApprox,
    const TExternalLabelsHelper& externalLabelsHelper,
    TVector<TVector<double>>* resultApprox
) {
    resultApprox->resize(externalLabelsHelper.GetExternalApproxDimension());
    for (int classId = 0; classId < internalApprox.ysize(); ++classId) {
        int visibleId = externalLabelsHelper.GetExternalIndex(classId);
        (*resultApprox)[visibleId] = internalApprox[classId];
    }
}

TVector<TVector<double>> MakeExternalApprox(
    const TVector<TVector<double>>& internalApprox,
    const TExternalLabelsHelper& externalLabelsHelper
) {
    const double inf = std::numeric_limits<double>::infinity();
    TVector<TVector<double>> externalApprox(externalLabelsHelper.GetExternalApproxDimension(),
                                            TVector<double>(internalApprox.back().ysize(), -inf));
    MakeExternalApprox(internalApprox, externalLabelsHelper, &externalApprox);
    return externalApprox;
}

TVector<TString> ConvertTargetToExternalName(
    const TVector<float>& target,
    const TExternalLabelsHelper& externalLabelsHelper
) {
    TVector<TString> convertedTarget(target.ysize());

    if (externalLabelsHelper.IsInitialized()) {
        for (int targetIdx = 0; targetIdx < target.ysize(); ++targetIdx) {
            convertedTarget[targetIdx] = externalLabelsHelper.GetVisibleClassNameFromLabel(target[targetIdx]);
        }
    } else {
        for (int targetIdx = 0; targetIdx < target.ysize(); ++targetIdx) {
            convertedTarget[targetIdx] = ToString<float>(target[targetIdx]);
        }
    }

    return convertedTarget;
}

TVector<TString> ConvertTargetToExternalName(
    const TVector<float>& target,
    const TFullModel& model
) {
    const auto& externalLabelsHelper = BuildLabelsHelper<TExternalLabelsHelper>(model);
    return ConvertTargetToExternalName(target, externalLabelsHelper);
}

TVector<TVector<double>> PrepareEvalForInternalApprox(
    const EPredictionType predictionType,
    const TFullModel& model,
    const TVector<TVector<double>>& approx,
    int threadCount
) {
    NPar::TLocalExecutor executor;
    executor.RunAdditionalThreads(threadCount - 1);
    return PrepareEvalForInternalApprox(predictionType, model, approx, &executor);
}

TVector<TVector<double>> PrepareEvalForInternalApprox(
    const EPredictionType predictionType,
    const TFullModel& model,
    const TVector<TVector<double>>& approx,
    NPar::TLocalExecutor* localExecutor
) {
    const auto& externalLabelsHelper = BuildLabelsHelper<TExternalLabelsHelper>(model);
    CB_ENSURE(externalLabelsHelper.IsInitialized() == IsMulticlass(approx),
              "Inappropriate usage of visible label helper: it MUST be initialized ONLY for multiclass problem");
    const auto& externalApprox = externalLabelsHelper.IsInitialized() ?
                                 MakeExternalApprox(approx, externalLabelsHelper) : approx;
    return PrepareEval(predictionType, externalApprox, localExecutor);
}

TVector<TVector<double>> PrepareEval(const EPredictionType predictionType,
                                     const TVector<TVector<double>>& approx,
                                     int threadCount) {
    NPar::TLocalExecutor executor;
    executor.RunAdditionalThreads(threadCount - 1);
    return PrepareEval(predictionType, approx, &executor);
}

void PrepareEval(const EPredictionType predictionType,
                 const TVector<TVector<double>>& approx,
                 NPar::TLocalExecutor* executor,
                 TVector<TVector<double>>* result) {

    switch (predictionType) {
        case EPredictionType::Probability:
            if (IsMulticlass(approx)) {
                *result = CalcSoftmax(approx, executor);
            } else {
                *result = {CalcNegativeSigmoid(approx[0]), CalcSigmoid(approx[0])};
            }
            break;
        case EPredictionType::LogProbability:
            if (IsMulticlass(approx)) {
                *result = CalcLogSoftmax(approx, executor);
            } else {
                *result = {CalcNegativeLogSigmoid(approx[0]), CalcLogSigmoid(approx[0])};
            }
            break;
        case EPredictionType::Class:
            result->resize(1);
            (*result)[0].reserve(approx.size());
            if (IsMulticlass(approx)) {
                TVector<int> predictions = {SelectBestClass(approx, executor)};
                (*result)[0].assign(predictions.begin(), predictions.end());
            } else {
                for (const double prediction : approx[0]) {
                    (*result)[0].push_back(prediction > 0);
                }
            }
            break;
        case EPredictionType::RawFormulaVal:
            *result = approx;
            break;
        default:
            Y_ASSERT(false);
    }
}

TVector<TVector<double>> PrepareEval(const EPredictionType predictionType,
                                     const TVector<TVector<double>>& approx,
                                     NPar::TLocalExecutor* localExecutor) {
    TVector<TVector<double>> result;
    PrepareEval(predictionType, approx, localExecutor, &result);
    return result;
}
