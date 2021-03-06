//
// Copyright 2019 ZetaSQL Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef ZETASQL_PUBLIC_COERCER_H_
#define ZETASQL_PUBLIC_COERCER_H_

#include "zetasql/public/function.h"
#include "zetasql/public/language_options.h"
#include "zetasql/public/signature_match_result.h"
#include "zetasql/public/type.h"
#include "zetasql/public/value.h"
#include "absl/time/time.h"

namespace zetasql {

// These classes provide the logic for the implicit and explicit type coercions
// allowed by ZetaSQL.  The full specification for casting and coercion is
// at:
//
//   (broken link)
//
// The coercer defines common supertypes for individual types (including
// n-ary supertypes) and whether one type can be coerced to another type.
// Different rules exist for the coercion of literals vs. general expressions,
// and NULL values are handled separately.  Coercion analysis identifies how
// close two types are to each other based on the documented type specificities.

// Returns the cost of coercing <literal_value> to Type <to_type>.  The cost
// to coerce NULL values is 1, while the cost of coercing non-NULL values
// depends on their types as per Type::GetTypeCoercionCost().
int GetLiteralCoercionCost(const Value& literal_value, const Type* to_type);


class Coercer {
 public:
  // Does not take ownership of <type_factory>.  The <default_timezone> is
  // used for coercions between dates/strings and timestamps.
  // <*language_options> should outlive this Coercer.
  Coercer(TypeFactory* type_factory, const absl::TimeZone default_timezone,
          const LanguageOptions* language_options)
      : type_factory_(type_factory),
        default_timezone_(default_timezone),
        language_options_(*language_options) {}

  Coercer(const Coercer&) = delete;
  Coercer& operator=(const Coercer&) = delete;

  ~Coercer() {}

  // The methods below only look at the type of an InputArgument and whether it
  // is a parameter or literal. They do not depend on the value of a
  // literal. That logic (e.g., for detecting that a very large int64_t struct
  // field cannot be coerced to an int32_t field) is in
  // FunctionResolver::ConvertLiteralToType().

  // Returns the common super type of the types present in <argument_set>.
  // Returns NULL if there is no common supertype for all the argument types in
  // the set.
  //
  // InputArgumentTypeSet has a special property where we can fetch the first
  // non-NULL argument inserted into <argument_set>.
  // This first non-NULL argument is special for computing supertypes for two
  // reasons:
  //  - Struct supertypes always use field aliases from the first non-NULL
  //    argument only.
  //  - For equivalent proto types (e.g. different versions of the same proto),
  //    we consider the first non-NULL proto argument as the supertype.
  const Type* GetCommonSuperType(
      const InputArgumentTypeSet& argument_set) const;

  // Returns whether <from_argument> can be coerced to <to_type>, for
  // either explicit or implicit coercion.  The <result> is updated
  // appropriately depending on whether coercion succeeds or fails.  If
  // failure, <result->non_matched_arguments> is incremented.  If success,
  // the appropriate <result> number of arguments successfully coerced is
  // incremented and the <result> distance is updated to reflect how 'close'
  // the types were (same types have distance 0, lower distance indicates
  // closer types and a better match).
  bool CoercesTo(const InputArgumentType& from_argument, const Type* to_type,
                 bool is_explicit, SignatureMatchResult* result) const;

  // Allows everything that CoercesTo allows plus the following two rules:
  // * INT64 -> INT32
  // * UINT64 -> UINT32
  // This is intended to allow statements like
  // "UPDATE Table SET int32_col = int32_col + 1" but as a side effect it will
  // also allow statements like "UPDATE Table SET int32_col = int64_expr".
  bool AssignableTo(const InputArgumentType& from_argument, const Type* to_type,
                    bool is_explicit, SignatureMatchResult* result) const;

 private:
  // Returns the common super type of the types present in <argument_set>, if
  // any. During supertype analysis, <treat_parameters_as_literals> determines
  // whether parameters are included with non-literals when identifying
  // common supertype candidates, or whether they are treated like
  // literals and are checked to see if they coerce to the candidate
  // supertypes.
  const Type* GetCommonSuperTypeImpl(const InputArgumentTypeSet& argument_set,
                                     bool treat_parameters_as_literals) const;

  // Returns whether <from_type> can be coerced to <to_type>, for
  // either explicit or implicit coercion.  Does not consider if <from_type>
  // is a literal.  The <result> is updated appropriately to reflect
  // success or failure as described for CoercesTo().
  bool TypeCoercesTo(const Type* from_type, const Type* to_type,
                     bool is_explicit, SignatureMatchResult* result) const;

  // Returns whether <struct_argument> can be coerced to <to_type>. We
  // consider <struct_argument> types individually to see whether they can be
  // coerced to <to_type> field types implicitly/explicitly. Field names are
  // irrelevant. The <result> is updated to reflect success or failure.
  // If <coerced_value> != nullptr and the input struct is a literal, then
  // the coerced input struct value is returned in <coerced_value>.
  //
  // Note that <struct_argument> optionally contains a list of field
  // InputArgumentTypes.  This list is populated for literal or partially
  // literal struct values being coerced.  If this list is not present,
  // then <struct_argument> represents a non-literal and its field types
  // are considered as non-literal field types from the StructType.
  //
  // TODO: Remove <coerced_value> here.
  bool StructCoercesTo(const InputArgumentType& struct_argument,
                       const Type* to_type, bool is_explicit,
                       SignatureMatchResult* result,
                       Value* coerced_value = nullptr) const;

  // Returns whether <array_argument> can be coerced to <to_type> for either
  // explicit or implicit coercion. <from_argument> must be an array type. For
  // explicit coercion or implicit conversion of a literal/parameter, the two
  // can be coerced if their element types can be coerced. For implicit
  // conversion of a non-literal/parameter, the two array types must be
  // equivalent. The <result> is updated appropriately to reflect success or
  // failure as described for CoercesTo().
  bool ArrayCoercesTo(const InputArgumentType& array_argument,
                      const Type* to_type, bool is_explicit,
                      SignatureMatchResult* result) const;

  // Returns whether a parameter of <from_type> can be coerced to <to_type>,
  // for either explicit or implicit coercion.  The <result> is updated
  // appropriately to reflect success or failure as described for CoercesTo().
  bool ParameterCoercesTo(const Type* from_type, const Type* to_type,
                          bool is_explicit, SignatureMatchResult* result) const;

  // Returns whether the literal Value can be coerced to <to_type> based
  // on implicit/explicit conversion rules.  The <result> is updated
  // appropriately to reflect success or failure as described for CoercesTo().
  // If <coerced_value> is not NULL, then it is updated to provide the coerced
  // value.
  //
  // TODO: Remove coerced_value here.
  bool LiteralCoercesTo(const Value& literal_value, const Type* to_type,
                        bool is_explicit, SignatureMatchResult* result,
                        Value* coerced_value = nullptr) const;

  // Returns the common struct super type of the <argument_set>.
  // When computing super type of struct types, we compute the super type for
  // each struct field individually and the final field aliases are determined
  // by the first non-NULL argument in <argument_set>.
  //
  // Returns NULL if there is no common supertype for all the argument types,
  // or if any of the arguments is a non-struct type.
  const StructType* GetCommonStructSuperType(
      const InputArgumentTypeSet& argument_set) const;

  // Returns the common super type of <arguments>. Returns NULL if there is no
  // common supertype for all the argument types, or if any of the arguments is
  // a non-array type.
  const ArrayType* GetCommonArraySuperType(
      const InputArgumentTypeSet& argument_set,
      bool treat_query_parameters_as_literals) const;

  // Strips off all the field aliases present inside <struct_type> (including
  // nested structs).
  void StripFieldAliasesFromStructType(const Type** struct_type) const;

  TypeFactory* type_factory_;  // Not owned.

  // Used for coercions between dates/strings and timestamps.  Not relevant
  // for other coercions.
  const absl::TimeZone default_timezone_;
  const LanguageOptions& language_options_;  // Not owned.
  friend class CoercerTest;
};

}  // namespace zetasql

#endif  // ZETASQL_PUBLIC_COERCER_H_
