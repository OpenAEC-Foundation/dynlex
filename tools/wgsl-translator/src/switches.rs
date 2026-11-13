// WGSL has no nonempty case fall-through. Keep each body once, using a
// single default case to retain the original switch's break/continue targets.
use naga::{
    BinaryOperator, Block, Expression, Function, Handle, LocalVariable, Module, Scalar, Span,
    Statement, SwitchCase, SwitchValue, Type, TypeInner, UnaryOperator,
};

fn expression(function: &mut Function, value: Expression) -> Handle<Expression> {
    function.expressions.append(value, Span::UNDEFINED)
}

fn emit(function: &mut Function, block: &mut Block, value: Expression) -> Handle<Expression> {
    let handle = expression(function, value);
    block.push(
        Statement::Emit(naga::Range::new_from_bounds(handle, handle)),
        Span::UNDEFINED,
    );
    handle
}

fn binary(
    function: &mut Function,
    block: &mut Block,
    op: BinaryOperator,
    left: Handle<Expression>,
    right: Handle<Expression>,
) -> Handle<Expression> {
    emit(function, block, Expression::Binary { op, left, right })
}

fn flag(function: &mut Function, boolean: Handle<Type>, name: &str) -> Handle<Expression> {
    let local = function.local_variables.append(
        LocalVariable {
            name: Some(name.into()),
            ty: boolean,
            init: None,
        },
        Span::UNDEFINED,
    );
    expression(function, Expression::LocalVariable(local))
}

fn store(block: &mut Block, pointer: Handle<Expression>, value: Handle<Expression>) {
    block.push(Statement::Store { pointer, value }, Span::UNDEFINED);
}

fn lower_switch(
    function: &mut Function,
    boolean: Handle<Type>,
    selector: Handle<Expression>,
    cases: Vec<SwitchCase>,
) -> Block {
    let mut result = Block::new();
    let next = flag(function, boolean, "switch_fallthrough");
    let done = flag(function, boolean, "switch_finished");
    let yes = expression(function, Expression::Literal(naga::Literal::Bool(true)));
    let no = expression(function, Expression::Literal(naga::Literal::Bool(false)));
    store(&mut result, next, no);
    store(&mut result, done, no);
    let mut matches = Vec::new();
    let mut default = yes;
    for case in &cases {
        let literal = match case.value {
            SwitchValue::I32(value) => Some(naga::Literal::I32(value)),
            SwitchValue::U32(value) => Some(naga::Literal::U32(value)),
            SwitchValue::Default => None,
        };
        if let Some(literal) = literal {
            let value = expression(function, Expression::Literal(literal));
            let equal = binary(
                function,
                &mut result,
                BinaryOperator::Equal,
                selector,
                value,
            );
            let different = emit(
                function,
                &mut result,
                Expression::Unary {
                    op: UnaryOperator::LogicalNot,
                    expr: equal,
                },
            );
            default = binary(
                function,
                &mut result,
                BinaryOperator::LogicalAnd,
                default,
                different,
            );
            matches.push(Some(equal));
        } else {
            matches.push(None);
        }
    }
    let mut body = Block::new();
    for (case, matching) in cases.into_iter().zip(matches) {
        let falling = emit(function, &mut body, Expression::Load { pointer: next });
        let finished = emit(function, &mut body, Expression::Load { pointer: done });
        let available = emit(
            function,
            &mut body,
            Expression::Unary {
                op: UnaryOperator::LogicalNot,
                expr: finished,
            },
        );
        let entered = binary(
            function,
            &mut body,
            BinaryOperator::LogicalOr,
            matching.unwrap_or(default),
            falling,
        );
        let condition = binary(
            function,
            &mut body,
            BinaryOperator::LogicalAnd,
            available,
            entered,
        );
        let mut accept = Block::new();
        store(&mut accept, next, if case.fall_through { yes } else { no });
        store(&mut accept, done, if case.fall_through { no } else { yes });
        accept.extend_block(case.body);
        body.push(
            Statement::If {
                condition,
                accept,
                reject: Block::new(),
            },
            Span::UNDEFINED,
        );
    }
    result.push(
        Statement::Switch {
            selector,
            cases: vec![SwitchCase {
                value: SwitchValue::Default,
                body,
                fall_through: false,
            }],
        },
        Span::UNDEFINED,
    );
    result
}

fn block(
    function: &mut Function,
    boolean: Handle<Type>,
    input: Block,
    changed: &mut bool,
) -> Block {
    let mut result = Block::new();
    for (mut statement, span) in input.span_into_iter() {
        match &mut statement {
            Statement::Block(body) => {
                *body = block(function, boolean, std::mem::take(body), changed)
            }
            Statement::If { accept, reject, .. } => {
                *accept = block(function, boolean, std::mem::take(accept), changed);
                *reject = block(function, boolean, std::mem::take(reject), changed);
            }
            Statement::Loop {
                body, continuing, ..
            } => {
                *body = block(function, boolean, std::mem::take(body), changed);
                *continuing = block(function, boolean, std::mem::take(continuing), changed);
            }
            Statement::Switch { selector, cases } => {
                for case in cases.iter_mut() {
                    case.body = block(function, boolean, std::mem::take(&mut case.body), changed);
                }
                if cases
                    .iter()
                    .any(|case| case.fall_through && !case.body.is_empty())
                {
                    *changed = true;
                    result.extend_block(lower_switch(
                        function,
                        boolean,
                        *selector,
                        std::mem::take(cases),
                    ));
                    continue;
                }
            }
            _ => {}
        }
        result.push(statement, span);
    }
    result
}

fn needs_lowering(body: &Block) -> bool {
    body.iter().any(|statement| match statement {
        Statement::Block(body) => needs_lowering(body),
        Statement::If { accept, reject, .. } => needs_lowering(accept) || needs_lowering(reject),
        Statement::Loop {
            body, continuing, ..
        } => needs_lowering(body) || needs_lowering(continuing),
        Statement::Switch { cases, .. } => cases
            .iter()
            .any(|case| (case.fall_through && !case.body.is_empty()) || needs_lowering(&case.body)),
        _ => false,
    })
}

pub(crate) fn lower(module: &mut Module) -> bool {
    if !module
        .functions
        .iter()
        .any(|(_, function)| needs_lowering(&function.body))
        && !module
            .entry_points
            .iter()
            .any(|entry| needs_lowering(&entry.function.body))
    {
        return false;
    }
    let boolean = module.types.insert(
        Type {
            name: None,
            inner: TypeInner::Scalar(Scalar::BOOL),
        },
        Span::UNDEFINED,
    );
    let mut changed = false;
    for (_, function) in module.functions.iter_mut() {
        let body = std::mem::take(&mut function.body);
        function.body = block(function, boolean, body, &mut changed);
    }
    for entry in &mut module.entry_points {
        let function = &mut entry.function;
        let body = std::mem::take(&mut function.body);
        function.body = block(function, boolean, body, &mut changed);
    }
    changed
}
