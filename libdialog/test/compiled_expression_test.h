#ifndef TEST_COMPILED_EXPRESSION_TEST_H_
#define TEST_COMPILED_EXPRESSION_TEST_H_

#include "compiled_expression.h"
#include "gtest/gtest.h"

using namespace ::dialog;

class CompiledExpressionTest : public testing::Test {
 public:
  static schema_t<storage::in_memory> s;

  struct rec {
    bool a;
    char b;
    short c;
    int d;
    long e;
    float f;
    double g;
    char h[16];
  } __attribute__((packed));

  static rec r;

  static schema_t<storage::in_memory> schema() {
    schema_builder builder;
    builder.add_column(bool_type(), "a");
    builder.add_column(char_type(), "b");
    builder.add_column(short_type(), "c");
    builder.add_column(int_type(), "d");
    builder.add_column(long_type(), "e");
    builder.add_column(float_type(), "f");
    builder.add_column(double_type(), "g");
    builder.add_column(string_type(16), "h");
    return schema_t<storage::in_memory>(".", builder.get_columns());
  }

  record_t record(bool a, char b, short c, int d, long e, float f, double g) {
    r = { a, b, c, d, e, f, g, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0 } };
    return s.apply(0, &r, sizeof(rec), 0);
  }

  static compiled_predicate predicate(const std::string& attr, relop_id id,
                                      const std::string& value) {
    predicate_t p;
    p.attr = attr;
    p.op = id;
    p.value = value;
    return compiled_predicate(p, s);
  }
};

CompiledExpressionTest::rec CompiledExpressionTest::r;

schema_t<storage::in_memory> CompiledExpressionTest::s = schema();

TEST_F(CompiledExpressionTest, TestRecordTest) {
  minterm m1, m2, m3;
  m1.add(predicate("a", relop_id::EQ, "true"));
  m1.add(predicate("b", relop_id::LT, "c"));

  m2.add(predicate("c", relop_id::LE, "10"));
  m2.add(predicate("d", relop_id::GT, "100"));

  m3.add(predicate("e", relop_id::GE, "1000"));
  m3.add(predicate("f", relop_id::NEQ, "100.3"));
  m3.add(predicate("g", relop_id::LT, "194.312"));

  compiled_expression cexp;
  cexp.insert(m1);
  cexp.insert(m2);
  cexp.insert(m3);

  cexp.test(record(true, 'a', 10, 101, 1000, 102.4, 182.3));
}

#endif /* TEST_COMPILED_EXPRESSION_TEST_H_ */