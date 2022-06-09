<?php

/**
 * @file
 * The administration forms, and their Validate and Submit functions.
 */

/**
 * Payment method callback.
 */
function uc_gc_client_uc_payment_method_callback($op, &$order, $form = NULL, &$form_state = NULL) {
  switch ($op) {
    case 'settings':
      if (isset($form_state['values']['uc_gc_client_sandbox'])) {
        $sandbox = $form_state['values']['uc_gc_client_sandbox'];
      }
      else {
        $sandbox = variable_get('uc_gc_client_sandbox', TRUE);
      }
      $sandbox ? $ext = '_sandbox' : $ext = '_live';
      $auth = uc_gc_client_authenticate();
      if ($auth == 200) {
        $response = uc_gc_client_get();
        isset($response->environ) ? $connected = TRUE : $connected = FALSE;
      }
      else {
        $connected = FALSE;
      }

      $form = array(
        '#prefix' => '<div id="connect-wrapper">',
        '#suffix' => '</div>',
      );

      $form['connect'] = array(
        '#type' => 'fieldset',
        '#title' => t('Connect with GoCardless'),
        '#collapsible' => FALSE,
        '#collapsed' => FALSE,
      );

      $form['connect']['uc_gc_client_sandbox'] = array(
        '#type' => 'checkbox',
        '#title' => '<b>' . t('Enable Sandbox') . '</b>',
        '#description' => t('Sandbox: GoCardless will operate in a test environment, and no real banking activity will occur.'),
        '#default_value' => $sandbox,
        '#ajax' => array(
          'callback' => 'uc_gc_client_sandbox_callback',
          'wrapper' => 'connect-wrapper',
          'method' => 'replace',
        ),
      );

      $markup = '<p><b>' . t('Connect / disconnect with GoCardless') . '</b></p>';
      if (!$connected) {
        $markup .= '<p>' . t("After clicking 'Connect' you will be redirected to the GoCardless where you can create an account and connect your site as a client of Seamless-CMS.co.uk") . '</p>';
        $form['connect']['markup'] = array(
          '#markup' => $markup,
        );
        $form['connect']['connect'] = array(
          '#type' => 'submit',
          '#disabled' => !$sandbox && !isset($_SERVER['HTTPS']) ? TRUE : FALSE,
          '#value' => $sandbox ? t('Connect SANDBOX') : t('Connect LIVE'),
          '#submit' => array('uc_gc_client_connect_submit'),
          '#suffix' => !$sandbox && !isset($_SERVER['HTTPS']) ? '<br /><i>' . t('Site needs to be secure (https) before you can connect to GoCardless LIVE.') . '</i>' : NULL,
        );
      }
      else {
        $form['connect']['markup'] = array(
          '#markup' => $markup,
        );
        // GoCardless account verification status and onboarding link.
        $params = array(
          'endpoint' => 'creditors',
          'action' => 'list',
        );
        $result = uc_gc_client_api($params);
        if ($result && $result->response->status_code == 200) {
          $creditors = $result->response->body->creditors;
          if (!empty($creditors)) {
            $creditor = array_shift($creditors);
            if ($creditor->fx_payout_currency) {
              $fx_enabled = TRUE;
            }
            if ($creditor->verification_status == 'successful') {
              $verified_markup = '<div class="messages status">' . t('Your GoCardless account is verified.') .  '</div>';
            }
            else {
              $verification_status = $creditor->verification_status == 'in_review'? 'In review' : 'Action required';
              $client_url = url(current_path(), array('absolute' => TRUE));
              $partner_url = variable_get('uc_gc_client_partner_url');
              $onboarding_ext = $sandbox ? 'sandbox' : 'live';
              $onboarding_url = $partner_url . '/gc_partner/onboarding_direct?env=' . $onboarding_ext . '&client_url=' . $client_url;
              $verified_markup = '<div class="messages warning">' . t("Your GoCardless account verification status is '@verification_status'. <a href='@onboarding_url'>Click here</a> to complete GoCardless onboarding and get verified.", array(
                '@onboarding_url' => $onboarding_url,
                '@verification_status' => $verification_status,
              )) .  '</div>';
            }
            $form['connect']['verified'] = array(
              '#markup' => $verified_markup,
            );
          }
        }
        $form['connect']['ext'] = array(
          '#type' => 'value',
          '#value' => $ext,
        );
        $confirm = t('Are you sure you want to disconnect your site from GoCardless?');
        $form['connect']['disconnect'] = array(
          '#type' => 'submit',
          '#value' => $sandbox ? t('Disconnect SANDBOX') : t('Disconnect LIVE'),
          '#submit' => array('uc_gc_client_disconnect_submit'),
          '#attributes' => array('onclick' => 'if (!confirm("' . $confirm . '")) {return false;}'),
        );
      }
      if ($connected) {
        if (!$webhook_secret = variable_get('uc_gc_client_webhook' . $ext)) {
          $webhook_secret = uc_gc_client_api(array(
            'endpoint' => 'webhook_secret',
          ))->response;
          variable_set('uc_gc_client_webhook' . $ext, $webhook_secret);
        }
        global $base_url;
        $webhook_url = $base_url . '/gc_client/webhook';
        $gc_webhook_url = t('https://manage@env.gocardless.com/developers/webhook-endpoints', [
          '@env' => $sandbox ? '-sandbox' : '',
        ]);
        $webhook_secret_markup = "<p id='webhook_secret'><b>" . t('Webhook secret: ') . "</b>" . $webhook_secret . "</p>";
        $webhook_secret_markup .= '<p>' . t('To receive webhooks create / update a Webhook Endpoint at your GoCardless account <a target="new" href="@gc_webhook_url">here</a>, and set the Webhook URL as <i>@webhook_url</i>, and the Webhook Secret as the random 30 byte string that has been generated for you above.', [
          '@webhook_url' => $webhook_url,
          '@gc_webhook_url' => $gc_webhook_url,
        ]) . '</p>';
        $form['connect']['webhook_secret_markup'] = array(
          '#title' => 'Webhook secret',
          '#type' => 'markup',
          '#markup' => $webhook_secret_markup,
        );
        $form['connect']['webhook_submit'] = array(
          '#type' => 'submit',
          '#value' => t('Change secret'),
          '#suffix' => t('If you change this, and have already set up your webhook endpoint in your GoCardless account, you will need to update it there as well.'),
          '#ajax' => array(
            'callback' => 'uc_gc_client_webhook_secret_callback',
            'wrapper' => 'webhook_secret'
          ),
        );
      }

      // Global.
      $form['global'] = array(
        '#type' => 'fieldset',
        '#title' => t('Global settings'),
        '#collapsible' => FALSE,
        '#collapsed' => FALSE,
      );
      $form['global']['uc_gc_client_dom'] = array(
        '#type' => 'textfield',
        '#title' => t('Day(s) of month that first payments go out on'),
        '#default_value' => variable_get('uc_gc_client_dom'),
        '#size' => 25,
        '#maxlength' => 24,
        '#description' => t('Enter one or more days of the month, upon which debit payments will start. The system will automatically choose the next available date from those you have provided. Values must be seperated by a comma.<br />These values will be ignored if there is a valid Start Date set for individual products, or if they are set to create a payment immediately.'),
        '#required' => FALSE,
      );
      $form['global']['uc_gc_client_payments_tab'] = array(
        '#type' => 'checkbox',
        '#title' => t('Hide Payments tab'),
        '#default_value' => variable_get('uc_gc_client_payments_tab'),
        '#description' => t('Ubercart Payments do not work with GoCardless, so checking this will hide the Payments tab when viewing any orders created through GoCardless.'),
      );
      $currencies_description = '<p>' . t("If you use this option, mandates and payments will be created in the appropriate currency based on the country provided by the customer in their billing address. The payment amount for customers using the non-default store currency will be automatically adjusted using current real exchange rates provided by the GoCardless API. You must use <a href='/admin/store/settings/countries/gocardless'>this form</a> to set which countries can be used with GoCardless during checkout. Do not use this option if your store is set up to use multiple currencies anyway. To create payments in foreign currencies you must either enable the appropriate scheme(s), or anable FX payments for your GoCardless account. Enabling FX payments will allow you to collect payments in all the currencies covered by GoCardless. To enable these you need to contact help@gocardless.com.") . '</p>';
      if ($connected) {
        $currencies_description .= '<p>' . t("FX payments are currently <em>@fx_state</em> in your GoCardless account.", array(
          '@fx_state' => isset($fx_enabled) ? 'enabled' : 'disabled',
        )) . '</p>';
      }
      $form['global']['uc_gc_client_currencies'] = array(
        '#type' => 'checkbox',
        '#title' => t('Create payments in foreign currencies'),
        '#default_value' => variable_get('uc_gc_client_currencies'),
        '#description' => $currencies_description,
      );

      // Payments.
      $form['payments'] = array(
        '#type' => 'fieldset',
        '#title' => t('One-off Payments settings'),
        '#collapsible' => FALSE,
        '#collapsed' => FALSE,
      );
      $form['payments']['uc_gc_client_payment_limit'] = array(
        '#type' => 'textfield',
        '#title' => t('Maximum payments'),
        '#default_value' => variable_get('uc_gc_client_payment_limit', 3),
        '#size' => 3,
        '#maxlength' => 3,
        '#description' => t("The maximum number of payments that can be raised automatically, per order, per day. If the amount is exceeded, a warning email is sent to the specified address above. Leave unset for unlimitted."),
        '#element_validate' => array('element_validate_integer_positive'),
        '#required' => FALSE,
      );
      $form['payments']['uc_gc_client_preauth_adjs'] = array(
        '#type' => 'textarea',
        '#title' => t('Adjustment titles'),
        '#default_value' => variable_get('uc_gc_client_preauth_adjs'),
        '#description' => t('Provide a list of pre-defined Scheduled Adjustment titles. One per line.'),
        '#resizable' => FALSE,
        '#cols' => 20,
        '#rows' => 10,
        '#prefix' => '<div style="width:400px;">',
        '#suffix' => '</div>',
      );

      // Checkout options.
      $form['checkout'] = array(
        '#type' => 'fieldset',
        '#title' => t('Checkout settings'),
        '#collapsible' => FALSE,
        '#collapsed' => FALSE,
      );
      $form['checkout']['uc_gc_client_optional_checkout_review'] = array(
        '#title' => '<b>' . t('Optional Checkout Review page') . '</b>',
        '#type' => 'checkbox',
        '#default_value' => variable_get('uc_gc_client_optional_checkout_review'),
        '#required' => FALSE,
        '#description' => t('Check this to emit the Checkout Review page.') . '<br />' . t("Don't use this if you have the Ubercart Optional Checkout Review module installed, or you are using other payment methods in addition to GoCardless Client."),
      );
      $form['checkout']['uc_gc_client_checkout_label'] = array(
        '#type' => 'textfield',
        '#title' => t('Checkout button label'),
        '#description' => t('Customize the label of the final checkout button when the customer is about to pay.'),
        '#default_value' => variable_get('uc_gc_client_checkout_label'),
      );

      // Debugging options.
      $form['debug'] = array(
        '#type' => 'fieldset',
        '#title' => t('Debugging'),
        '#collapsible' => FALSE,
        '#collapsed' => FALSE,
      );
      $form['debug']['uc_gc_client_warnings_email'] = array(
        '#type' => 'textfield',
        '#title' => t('Email'),
        '#default_value' => variable_get('uc_gc_client_warnings_email'),
        '#description' => t('Email address to send warnings.'),
        '#size' => 40,
        '#maxlength' => 40,
      );
      $form['debug']['uc_gc_client_debug_webhook'] = array(
        '#prefix' => '<p>',
        '#suffix' => '</p>',
        '#type' => 'checkbox',
        '#title' => '<b>' . t('Enable webhook debugging') . '</b>',
        '#description' => t('Webhooks recieved from GoCardless will be written to the log.'),
        '#default_value' => variable_get('uc_gc_client_debug_webhook', FALSE),
      );
      $form['debug']['uc_gc_client_debug_api'] = array(
        '#prefix' => '<p>',
        '#suffix' => '</p>',
        '#type' => 'checkbox',
        '#title' => '<b>' . t('Enable API debugging') . '</b>',
        '#description' => t('Responses from the Partner site to API posts will be written to the log.'),
        '#default_value' => variable_get('uc_gc_client_debug_api', FALSE),
      );

      // Set message if returning from Partner site after connecting.
      if (isset($_GET['status'])) {
        if ($_GET['status'] == 'connected') {
          drupal_set_message(t('You have connected successfully with GoCardless'));
        }
        // $_GET['status'] will be "failed" if the wrong password was provided
        // when receiving the authentication credentials from the partner site
        // during the connection process. See uc_gc_client_connect().
        elseif ($_GET['status'] == 'failed') {
          drupal_set_message(t('Connection with GoCardless has failed'), 'error');
        }
      }
      $form['#submit'][] = 'uc_gc_client_admin_validate';
      return $form;
  }
}

/**
 * AJAX callback function for gc_client_uc_payment_method_callback().
 */
function uc_gc_client_sandbox_callback(&$form, &$form_state) {
  variable_set('uc_gc_client_sandbox', $form_state['values']['uc_gc_client_sandbox']);
  ctools_include('ajax');
  ctools_add_js('ajax-responder');
  $commands[] = ctools_ajax_command_reload();
  print ajax_render($commands);
  drupal_exit();
}

/**
 * AJAX callback function for gc_client_uc_payment_method_callback().
 *
 * On submitting 'Change secret' a new client secret is obtained, saved to
 * the module config and returned for display on the settings form.
 *
 * @return array
 *   The new render array for display on the settings form.
 */
function uc_gc_client_webhook_secret_callback(&$form, &$form_state) {
  drupal_set_message(t('You have changed your webhook secret. If you have already added a secret to your GoCardless account, you will have to update it there as well.'), 'warning');
  $ext = $form_state['values']['uc_gc_client_sandbox'] ? '_sandbox' : '_live';
  $webhook_secret = uc_gc_client_api(array(
    'endpoint' => 'webhook_secret',
  ))->response;
  variable_set('uc_gc_client_webhook' . $ext, $webhook_secret);
  $output = "<p id='webhook_secret'><b>" . t('Webhook secret: ') . "</b>" . $webhook_secret . "</p>";
  return array('#markup' => $output);
}

/**
 * Validates the gc_client payment method settings form.
 */
function uc_gc_client_admin_validate(&$form, &$form_state) {
  // Validate and process Days of Month.
  if (isset($form['uc_gc_client_dom']['#value'])) {
    // Only allow positive integers, spaces and commas.
    $doms = $form['gc_client_dom']['#value'];
    $regex = "/^[0-9, ]+$/";
    if (!preg_match($regex, $doms)) {
      form_set_error('uc_gc_client_dom', t('Only positve integers and commas please.'));
    }
    // Remove spaces.
    $doms = str_replace(' ', '', $doms);
    // Remove possibilty of extra comma at end.
    $doms = explode(', ', $doms);
    foreach ($doms as $dom => $value) {
      if (empty($value)) {
        unset($doms[$dom]);
      }
      if ($value > 28) {
        form_set_error('uc_gc_client_dom', t('Number must not be greater than 28'));
      }
    }
    // Sort ascending.
    sort($doms);
    $doms = implode(', ', $doms);
    // Save processed value.
    $form_state['values']['uc_gc_client_dom'] = $doms;
    variable_set('uc_gc_client_dom', $doms);
  }

  if (!valid_email_address($form_state['values']['uc_gc_client_warnings_email'])) {
    form_set_error('uc_gc_client_warnings_email', t('Please provide a valid email address.'));
  }
}

/**
 * Implements hook_form_submit().
 *
 * Redirect to Partner site to activate the GoCardless OAuth Flow.
 */
function uc_gc_client_connect_submit($form, $form_state) {
  $form_state['values']['uc_gc_client_sandbox'] == 1 ? $env = 'sandbox' : $env = 'live';
  global $base_url;
  $client_url = urlencode($base_url . '/admin/store/settings/payment/method/gc_client');
  $url = variable_get('uc_gc_client_partner_url') . '/gc_partner/connect/' . $env;
  $token = user_password(24);
  variable_set('uc_gc_client_user_pass_' . $env, $token);
  drupal_goto($url, array(
    'query' => array(
      'mail' => variable_get('site_mail', ''),
      'client_url' => $client_url,
      'token' => $token,
      'module' => 'Ubercart 7.x-2.x',
      'site_email' => variable_get('site_mail'),
    ),
  ));
}

/**
 * Implements hook_drupal_goto_alter().
 *
 * Closes the overlay when redirecting to an external url
 * Required when redirecting to Partner site at beginning of OAuth flow.
 */
function uc_gc_client_drupal_goto_alter(&$path, &$options, &$http_response_code) {
  if (module_exists('overlay') && overlay_get_mode() == 'child') {
    if (url_is_external($path)) {
      overlay_close_dialog($path, $options);
      overlay_deliver_empty_page();
    }
  }
}

/**
 * Implements hook_form_FORM_ID_alter().
 *
 * Add extra GoCardless settings to Product node Edit form.
 */
function uc_gc_client_form_node_form_alter(&$form, $form_state, $form_id) {
  if (uc_product_is_product_form($form)) {
    $nid = $form['nid']['#value'];
    $defaults = db_select('uc_gc_client_products', 'p')
      ->fields('p')
      ->condition('nid', $nid, '=')
      ->execute()
      ->fetchAssoc();

    $form['gc'] = array(
      '#type' => 'fieldset',
      '#title' => t('GoCardless Settings'),
      '#collapsible' => TRUE,
      '#collapsed' => TRUE,
      '#weight' => -12,
      '#attributes' => array('class' => array('gc-field')),
      '#group' => 'additional_settings',
      '#attached' => array(
        'js' => array(
          'vertical-tabs' => drupal_get_path('module', 'uc_gc_client') . '/uc_gc_client.js',
        ),
      ),
    );
    $options = array(
      'I' => t('Instant non-recurring payment'),
      'S' => t('Subscription'),
      'P' => t('One-off payments'),
    );
    $form['gc']['gc_type'] = array(
      '#type' => 'select',
      '#title' => t('Payment type'),
      '#options' => $options,
      '#default_value' => isset($defaults['type']) ? $defaults['type'] : NULL,
      '#description' => t("<p>Select the payment type that will be used by GoCardless for this product.</p><p>Instant non-recurring payments are single payments created upon checkout by GoCardless using open banking protocol and do not require a debit mandate. Open banking does not require credit/debit cards but the experience is similar to other online payment services in that the customer is required to provide their bank details, and to verify the bank account.</p><p>Subscription and One-off payments both require a debit mandate, which is created automatically upon checkout. Subscriptions are recurring payments that are created automatically by GoCardless according to the instructions that you provide here. One-off payments are (recurring) payments that are created automatically when your website instructs GoCardless to do so, and are more flexible than Subscription payments. Debit payments take several days to complete following an instruction from GoCardless to the bank to create the payment.</p><p>Instant payments are only created if none of the products in a cart are using recurrence rules, <i>or</i> if they are One-off payments that are configured to 'Raise payment immediately' upon checkout. Instant payments are currenly only available in the UK and Germany. If it is not possible to create an Instant payment upon checkout, a debit mandate will be created instead, and the product will be paid for with a One-off payment.</p>"),
      '#weight' => 0,
    );
    $form['gc']['container'] = array(
      '#type' => 'container',
      '#states' => array(
        'invisible' => array(
          'select[name="gc_type"]' => array('value' => 'I'),
        ),
      ),
    );
    $form['gc']['container']['interval_length'] = array(
      '#type' => 'textfield',
      '#title' => t('Interval length between payments'),
      '#default_value' => isset($defaults['interval_length']) ? $defaults['interval_length'] : NULL,
      '#weight' => 1,
      '#size' => 3,
      '#maxlength' => 3,
      '#description' => t('The default interval length between debit payments. This setting is required for Subscription payments.<br />It can be set for One-off payments, or leave unset if regular payments are not expected.<br />This value will be overridden if the Interval Attribute is set for a product.'),
      '#element_validate' => array('element_validate_integer_positive'),
      '#required' => FALSE,
    );
    $form['gc']['container']['interval_unit'] = array(
      '#type' => 'select',
      '#title' => t('Interval unit between payments'),
      '#default_value' => isset($defaults['interval_unit']) ? $defaults['interval_unit'] : NULL,
      '#weight' => 2,
      '#options' => array(
        'week' => t('week'),
        'month' => t('month'),
        'year' => t('year'),
      ),
      "#empty_option" => t('- None -'),
      '#description' => t('The default interval unit between debit payments. This setting is required for Subscription payments.<br />It can be set for One-off payments, or leave unset if regular payments are not expected.<br />This value will be overridden if the Interval attribute is set for a product.'),
      '#required' => FALSE,
    );
    $form['gc']['container']['price_x'] = array(
      '#type' => 'textfield',
      '#size' => 6,
      '#title' => t('Price multiplier'),
      '#default_value' => isset($defaults['price_x']) ? (float) $defaults['price_x'] : 1,
      '#description' => t('Set a multiplier for subscriptions and one-off payments. Use in examples where the GoCardless amount is different to the price, for example a monthly subscription for a weekly order. Leave as 1 if the GoCardless amount and the price are the same.'),
      '#weight' => 3,
      '#element_validate' => array('uc_gc_client_element_validate_positive_number'),
    );
    $form['gc']['container']['raise_payment'] = array(
      '#type' => 'checkbox',
      '#prefix' => t('<b>Raise payment immediately</b>'),
      '#weight' => 4,
      '#default_value' => !is_null($defaults['raise_payment']) ? $defaults['raise_payment'] : 0,
      '#title' => t("Automatically raise a payment upon completion of checkout"),
      '#description' => t('Checking this will override any other settings for raising the initial payment.<br />Leave unchecked if you want another module to handle raising the initial payment.') . '<br /><br />',
    );
    $form['gc']['container']['start_date'] = array(
      '#title' => t('Start date'),
      '#type' => module_exists('date_popup') ? 'date_popup' : 'date_select',
      '#date_format' => module_exists('date_popup') ? variable_get('date_format_uc_store') : 'd M Y',
      '#datepicker_options' => array('minDate' => 0),
      '#date_year_range' => '0:+1',
      '#default_value' => !is_null($defaults['start_date']) ? date('Y-m-d', $defaults['start_date']) : NULL,
      '#weight' => 5,
      '#description' => t("Specify a start date for the customer's new mandate. If this is enabled then the start date provided will override any other start date settings."),
      '#states' => array(
        'visible' => array(
          ':input[name="raise_payment"]' => array('checked' => FALSE),
        ),
      ),
    );
    $form['#validate'][] = 'uc_gc_client_node_form_validate';
    $form['#submit'][] = 'uc_gc_client_node_form_submit';
  }
}

/**
 * Implements hook_node_insert().
 *
 * Add extra GoCardless settings to {uc_gc_client_products} table.
 */
function uc_gc_client_node_insert($node) {
  if (uc_product_is_product($node)) {
    db_insert('uc_gc_client_products')
      ->fields(array(
        'nid' => $node->nid,
        'type' => !empty($node->gc_type) ? $node->gc_type : NULL,
        'price_x' => $node->price_x,
        'start_date' => !empty($node->start_date) ? $node->start_date : NULL,
        'interval_length' => !empty($node->interval_length) ? $node->interval_length : NULL,
        'interval_unit' => !empty($node->interval_unit) ? $node->interval_unit : NULL,
        'raise_payment' => !empty($node->raise_payment) ? 1 : 0,
      ))
      ->execute();
  }
}

/**
 * Implements hook_node_form_validate().
 */
function uc_gc_client_node_form_validate($form, &$form_state) {
  if (!empty($form_state['values']['interval_length']) && empty($form_state['values']['interval_unit'])) {
    form_set_error('interval_unit', t('Interval unit must be set if interval length is set.'));
  }
  if (!empty($form_state['values']['interval_unit']) && empty($form_state['values']['interval_length'])) {
    form_set_error('interval_length', t('Interval length must be set if interval unit is set.'));
  }
  if ($form_state['values']['gc_type'] == 'S' && empty($form_state['values']['interval_unit'])) {
    form_set_error('interval_unit', t('Interval unit must be set if type is Subscription.'));
  }
  if ($form_state['values']['gc_type'] == 'S' && empty($form_state['values']['interval_length'])) {
    form_set_error('interval_length', t('Interval length must be set if type is Subscription.'));
  }
}

/**
 * Implements hook_node_form_submit().
 *
 *  Updates gc_client_products table.
 */
function uc_gc_client_node_form_submit($form, &$form_state) {
  if (uc_product_is_product_form($form) && isset($form_state['values']['nid'])) {
    $nid = $form_state['values']['nid'];
    db_merge('uc_gc_client_products')
      ->key(array('nid' => $nid))
      ->fields(array(
        'nid' => $nid,
        'type' => !empty($form_state['values']['gc_type']) ? $form_state['values']['gc_type'] : NULL,
        'price_x' => $form_state['values']['price_x'],
        'start_date' => !is_null($form_state['values']['start_date']) ? strtotime($form_state['values']['start_date']) : NULL,
        'interval_length' => !empty($form_state['values']['interval_length']) ? $form_state['values']['interval_length'] : NULL,
        'interval_unit' => !empty($form_state['values']['interval_unit']) ? $form_state['values']['interval_unit'] : NULL,
        'raise_payment' => !empty($form_state['values']['raise_payment']) ? 1 : 0,
      ))
      ->execute();
  }
}

/**
 * Implements hook_node_delete().
 *
 * Delete row from {gc_client_products} table.
 */
function uc_gc_client_node_delete($node) {
  if (uc_product_is_product($node)) {
    db_delete('uc_gc_client_products')
      ->condition('nid', $node->nid, '=')
      ->execute();
  }
}

/**
 * Implements hook_field_extra_fields().
 *
 * Adds the "Product information".
 */
function uc_gc_client_field_extra_fields() {
  $extra = array();
  foreach (uc_product_types() as $type) {
    $extra['node'][$type] = array(
      'display' => array(
        'type' => array(
          'label' => t('GoCardless type'),
          'description' => t('Subscription or pre-authorization'),
          'weight' => 0,
        ),
        'start_date' => array(
          'label' => t('Start date for new mandate'),
          'description' => t('Start date for new mandate description'),
          'weight' => 2,
        ),
      ),
    );
  }
  return $extra;
}

/**
 * Implements hook_node_view().
 */
function uc_gc_client_node_view($node, $view_mode, $langcode) {
  if (uc_product_is_product($node)) {
    $nid = $node->nid;
    $extras = db_select('uc_gc_client_products', 'p')
      ->fields('p')
      ->condition('nid', $nid, '=')
      ->execute()
      ->fetchAssoc();

    if ($extras['start_date']) {
      $start_date = format_date($extras['start_date'], 'uc_store');
      $node->content['start_date'] = array(
        '#markup' => '<p>' . t('Your first debit payment will be paid on') . '<b>' . $start_date . '</b>.</p>',
        '#value' => $start_date,
      );
    }
  }
}

/**
 * Implements hook_form().
 *
 * Provides an interface to enable / disable GC countries.
 */
function uc_gc_client_countries_form($form, &$form_state) {
  $uc_countries = db_select('uc_countries', 'u')
    ->fields('u')
    ->condition('version', 0, '>')
    ->execute()->fetchAllAssoc('country_iso_code_2');

  $gc_countries = variable_get('uc_gc_client_countries');
  $countries = array();
  $default_value = array();
  foreach ($gc_countries as $gc_code => $gc_country) {
    if (isset($uc_countries[$gc_code])) {
      $country_name = $uc_countries[$gc_code]->country_name;
      $countries_[$country_name] = $gc_code;
    }
  }
  ksort($countries_);
  foreach ($countries_ as $name => $gc_code) {
    $countries[$gc_code] = array(
      $name,
      $gc_countries[$gc_code]['region'],
      $gc_countries[$gc_code]['sign'],
    );
    if ($gc_countries[$gc_code]['enabled']) {
      $default_value[$gc_code] = TRUE;
    }
  }
  $form = array();
  $form['markup'] = array(
    '#markup' => '<p>' . t('Countries must first be enabled in Ubercart before they can be enabled / disabled here.') . '</p>',
  );
  $header = array('Country', 'Debit scheme', 'Currency');
  $form['countries'] = array(
    '#type' => 'tableselect',
    '#header' => $header,
    '#options' => $countries,
    '#empty' => t('No countries available.'),
    '#default_value' => $default_value,
  );
  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Enable / disable countries'),
  );
  return $form;
}

/**
 * Implements hook_form_submit().
 */
function uc_gc_client_countries_form_submit($form, &$form_state) {
  $countries = $form_state['values']['countries'];
  $gc_countries = variable_get('uc_gc_client_countries');
  foreach ($countries as $code => $country) {
    if ($country) {
      $gc_countries[$code]['enabled'] = 1;
    }
    else {
      $gc_countries[$code]['enabled'] = 0;
    }
  }
  variable_set('uc_gc_client_countries', $gc_countries);
}
